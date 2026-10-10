// JACoop.exe - native launcher (replaces launcher\jacoop.ps1).
//
//   JACoop.exe                 host: main menu > COOPERATION > CREER (lobby), or solo + F6 > HEBERGER
//   JACoop.exe join [ip[:port]] joiner: straight into that host's lobby, or the COOPERATION > REJOINDRE menu
//   JACoop.exe solo            plain single player with the co-op engine, no networking
//
// Finds Jedi Academy (Steam library folders, GOG registry, usual folders, then
// asks with a folder dialog), remembers it in jacoop.ini next to the exe, and
// starts bin\openjk_sp.x86_64.exe with this folder as its home (config, saves).
// Nothing is ever written into the game's own folder.
//
// Build (x64, VS Build Tools):  build-launcher.cmd

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <stdio.h>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "comctl32.lib")

static std::wstring g_root;		// folder of the exe (the JACoop folder)
static std::wstring g_ini;

// French Windows -> French, anything else -> English (like the game's first language)
static const wchar_t *Tr( const wchar_t *fr, const wchar_t *en ) {
	return ( PRIMARYLANGID( GetUserDefaultUILanguage() ) == LANG_FRENCH ) ? fr : en;
}

static bool FileExists( const std::wstring &p ) {
	DWORD a = GetFileAttributesW( p.c_str() );
	return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY );
}
static bool DirExists( const std::wstring &p ) {
	DWORD a = GetFileAttributesW( p.c_str() );
	return a != INVALID_FILE_ATTRIBUTES && ( a & FILE_ATTRIBUTE_DIRECTORY );
}
static std::wstring Join( const std::wstring &a, const std::wstring &b ) {
	if ( a.empty() ) return b;
	if ( a.back() == L'\\' || a.back() == L'/' ) return a + b;
	return a + L"\\" + b;
}
static bool IsGameData( const std::wstring &p ) {
	return !p.empty() && FileExists( Join( p, L"base\\assets0.pk3" ) ) && FileExists( Join( p, L"base\\assets3.pk3" ) );
}

// ---- jacoop.ini (key=value) ----
static std::wstring IniGet( const wchar_t *key ) {
	wchar_t buf[MAX_PATH * 2] = { 0 };
	GetPrivateProfileStringW( L"JACoop", key, L"", buf, (DWORD)( sizeof( buf ) / sizeof( buf[0] ) ), g_ini.c_str() );
	if ( !buf[0] ) {	// older launcher wrote bare key=value lines (no section)
		FILE *f = _wfopen( g_ini.c_str(), L"rt, ccs=UTF-8" );
		if ( f ) {
			wchar_t line[MAX_PATH * 2];
			size_t klen = wcslen( key );
			while ( fgetws( line, (int)( sizeof( line ) / sizeof( line[0] ) ), f ) ) {
				if ( !_wcsnicmp( line, key, klen ) && line[klen] == L'=' ) {
					wcscpy( buf, line + klen + 1 );
					size_t n = wcslen( buf );
					while ( n && ( buf[n - 1] == L'\n' || buf[n - 1] == L'\r' || buf[n - 1] == L' ' ) ) buf[--n] = 0;
					break;
				}
			}
			fclose( f );
		}
	}
	return buf;
}
static void IniSet( const wchar_t *key, const std::wstring &value ) {
	WritePrivateProfileStringW( L"JACoop", key, value.c_str(), g_ini.c_str() );
}

// ---- registry helpers ----
static std::wstring RegString( HKEY root, const wchar_t *subkey, const wchar_t *value, REGSAM extra = 0 ) {
	HKEY h;
	std::wstring out;
	if ( RegOpenKeyExW( root, subkey, 0, KEY_READ | extra, &h ) == ERROR_SUCCESS ) {
		wchar_t buf[MAX_PATH * 2];
		DWORD size = sizeof( buf ), type = 0;
		if ( RegQueryValueExW( h, value, NULL, &type, (LPBYTE)buf, &size ) == ERROR_SUCCESS && ( type == REG_SZ || type == REG_EXPAND_SZ ) ) {
			buf[size / sizeof( wchar_t )] = 0;
			out = buf;
		}
		RegCloseKey( h );
	}
	return out;
}

static void AddSteamLibraries( const std::wstring &steam, std::vector<std::wstring> &cand ) {
	if ( steam.empty() ) return;
	cand.push_back( Join( steam, L"steamapps\\common\\Jedi Academy\\GameData" ) );
	FILE *f = _wfopen( Join( steam, L"steamapps\\libraryfolders.vdf" ).c_str(), L"rb" );
	if ( !f ) return;
	std::string text;
	char buf[4096];
	size_t n;
	while ( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 ) text.append( buf, n );
	fclose( f );
	// "path"   "D:\\SteamLibrary"
	size_t pos = 0;
	while ( ( pos = text.find( "\"path\"", pos ) ) != std::string::npos ) {
		size_t q1 = text.find( '"', pos + 6 );
		if ( q1 == std::string::npos ) break;
		size_t q2 = text.find( '"', q1 + 1 );
		if ( q2 == std::string::npos ) break;
		std::string p = text.substr( q1 + 1, q2 - q1 - 1 );
		std::string u;
		for ( size_t i = 0; i < p.size(); i++ ) {	// unescape \\ -> \ .
			if ( p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\' ) { u += '\\'; i++; }
			else u += p[i];
		}
		int wn = MultiByteToWideChar( CP_UTF8, 0, u.c_str(), -1, NULL, 0 );
		std::wstring w( wn ? wn - 1 : 0, 0 );
		if ( wn ) MultiByteToWideChar( CP_UTF8, 0, u.c_str(), -1, &w[0], wn );
		cand.push_back( Join( w, L"steamapps\\common\\Jedi Academy\\GameData" ) );
		pos = q2;
	}
}

static void AddGogGames( HKEY root, const wchar_t *subkey, REGSAM extra, std::vector<std::wstring> &cand ) {
	HKEY h;
	if ( RegOpenKeyExW( root, subkey, 0, KEY_READ | extra, &h ) != ERROR_SUCCESS ) return;
	wchar_t name[256];
	for ( DWORD i = 0;; i++ ) {
		DWORD len = 256;
		if ( RegEnumKeyExW( h, i, name, &len, NULL, NULL, NULL, NULL ) != ERROR_SUCCESS ) break;
		std::wstring sub = std::wstring( subkey ) + L"\\" + name;
		std::wstring gameName = RegString( root, sub.c_str(), L"gameName", extra );
		std::wstring path = RegString( root, sub.c_str(), L"path", extra );
		if ( !path.empty() && ( gameName.find( L"Jedi Academy" ) != std::wstring::npos || gameName.find( L"JEDI ACADEMY" ) != std::wstring::npos || gameName.find( L"Jedi Knight" ) != std::wstring::npos ) ) {
			cand.push_back( Join( path, L"GameData" ) );
			cand.push_back( path );
		}
	}
	RegCloseKey( h );
}

static std::wstring FindGameData() {
	std::wstring cfg = IniGet( L"GameData" );
	if ( IsGameData( cfg ) ) return cfg;

	std::vector<std::wstring> cand;
	AddSteamLibraries( RegString( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath" ), cand );
	AddSteamLibraries( RegString( HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath" ), cand );
	AddSteamLibraries( RegString( HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"SteamPath" ), cand );
	AddGogGames( HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0, cand );
	AddGogGames( HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", 0, cand );

	const wchar_t *dirs[] = { L"%ProgramFiles%", L"%ProgramFiles(x86)%", L"C:\\GOG Games", L"D:\\GOG Games", L"C:\\Games", L"D:\\Games", L"E:\\Games", L"C:\\SteamLibrary", L"D:\\SteamLibrary", L"E:\\SteamLibrary", L"F:\\SteamLibrary" };
	const wchar_t *subs[] = { L"GOG Galaxy\\Games\\STAR WARS Jedi Knight - Jedi Academy\\GameData", L"Star Wars Jedi Knight - Jedi Academy\\GameData", L"STAR WARS Jedi Knight - Jedi Academy\\GameData",
		L"Jedi Academy\\GameData", L"steamapps\\common\\Jedi Academy\\GameData", L"LucasArts\\Star Wars Jedi Knight Jedi Academy\\GameData" };
	for ( const wchar_t *d : dirs ) {
		wchar_t exp[MAX_PATH];
		ExpandEnvironmentStringsW( d, exp, MAX_PATH );
		if ( exp[0] == L'%' ) continue;	// unexpanded
		for ( const wchar_t *s : subs ) cand.push_back( Join( exp, s ) );
	}
	for ( const std::wstring &c : cand ) {
		if ( IsGameData( c ) ) { IniSet( L"GameData", c ); return c; }
	}

	// ask
	MessageBoxW( NULL, Tr( L"Jedi Academy introuvable (Steam / GOG).\nIndique le dossier GameData du jeu (celui qui contient base\\assets0.pk3).",
		L"Jedi Academy not found (Steam / GOG).\nPoint to the game's GameData folder (the one holding base\\assets0.pk3)." ), L"JACoop", MB_OK | MB_ICONINFORMATION );
	CoInitialize( NULL );
	BROWSEINFOW bi = { 0 };
	bi.lpszTitle = Tr( L"Dossier GameData de Jedi Academy", L"Jedi Academy GameData folder" );
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
	LPITEMIDLIST pidl = SHBrowseForFolderW( &bi );
	if ( pidl ) {
		wchar_t path[MAX_PATH];
		if ( SHGetPathFromIDListW( pidl, path ) ) {
			std::wstring p = path;
			if ( IsGameData( p ) ) { IniSet( L"GameData", p ); CoTaskMemFree( pidl ); return p; }
			if ( IsGameData( Join( p, L"GameData" ) ) ) { p = Join( p, L"GameData" ); IniSet( L"GameData", p ); CoTaskMemFree( pidl ); return p; }
		}
		CoTaskMemFree( pidl );
	}
	MessageBoxW( NULL, Tr( L"Dossier GameData invalide (base\\assets0.pk3 et base\\assets3.pk3 attendus). Relance JACoop.exe.",
		L"Invalid GameData folder (base\\assets0.pk3 and base\\assets3.pk3 expected). Start JACoop.exe again." ), L"JACoop", MB_OK | MB_ICONERROR );
	return L"";
}

// an older version kept the saves under base\saves; they live at the root now
static void MoveOldSaves() {
	std::wstring oldDir = Join( g_root, L"base\\saves" ), newDir = Join( g_root, L"saves" );
	if ( !DirExists( oldDir ) ) return;
	CreateDirectoryW( newDir.c_str(), NULL );
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW( Join( oldDir, L"*" ).c_str(), &fd );
	if ( h == INVALID_HANDLE_VALUE ) return;
	do {
		if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) continue;
		std::wstring dst = Join( newDir, fd.cFileName );
		if ( !FileExists( dst ) ) MoveFileW( Join( oldDir, fd.cFileName ).c_str(), dst.c_str() );
	} while ( FindNextFileW( h, &fd ) );
	FindClose( h );
	RemoveDirectoryW( oldDir.c_str() );	// only succeeds once empty
}

// advjk://join/<ip[:port]> one-click join from the website. The scheme is
// registered under HKCU (no admin, self-healing) on every launch so simply
// moving the JACoop folder keeps the links working.
static void RegisterJoinProtocol( const wchar_t *exePath ) {
	HKEY h;
	if ( RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Classes\\advjk", 0, NULL, 0, KEY_WRITE, NULL, &h, NULL ) == ERROR_SUCCESS ) {
		const wchar_t *desc = L"URL:AdvancedJK Coop";
		RegSetValueExW( h, NULL, 0, REG_SZ, (const BYTE *)desc, (DWORD)( wcslen( desc ) + 1 ) * sizeof( wchar_t ) );
		RegSetValueExW( h, L"URL Protocol", 0, REG_SZ, (const BYTE *)L"", sizeof( wchar_t ) );
		RegCloseKey( h );
	}
	if ( RegCreateKeyExW( HKEY_CURRENT_USER, L"Software\\Classes\\advjk\\shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &h, NULL ) == ERROR_SUCCESS ) {
		std::wstring cmd = L"\"" + std::wstring( exePath ) + L"\" \"%1\"";
		RegSetValueExW( h, NULL, 0, REG_SZ, (const BYTE *)cmd.c_str(), (DWORD)( cmd.size() + 1 ) * sizeof( wchar_t ) );
		RegCloseKey( h );
	}
}

// the address lands unquoted in the engine command line (+set ui_coopJoin):
// keep it to characters a server address can actually contain so a crafted
// advjk:// link cannot smuggle extra +commands
static std::wstring SanitizeAddress( std::wstring a ) {
	std::wstring out;
	for ( wchar_t c : a ) {
		if ( iswalnum( c ) || c == L'.' || c == L'-' || c == L'_' || c == L':' || c == L'[' || c == L']' ) {
			out += c;
		}
	}
	if ( out.size() > 64 ) out.resize( 64 );
	return out;
}

// ---- self-update ----
//
// The site serves an update feed at advjksite.koevetmc.workers.dev:
//   /coop/manifest.json        {"version":"sha","files":[{"name","size","md5"}]}
//   /coop/files/<name>         the payload bytes
//
// On launch: if jacoop-version.txt doesn't match the manifest, every payload
// whose on-disk bytes differ is downloaded to *.new, MD5-verified, then moved
// over the target (engine files land at the root AND under bin\ like the
// installer; the pk3 goes to base\). JACoop.exe itself can't be swapped while
// running — it stays staged as JACoop.exe.new and a spawned swapper applies
// it then relaunches. Any failure falls through to launching what's on disk.

static const wchar_t *UPD_HOST = L"advjksite.koevetmc.workers.dev";

struct UpdFile {
	std::string	name;
	DWORD		size;
	char		md5[33];	// lowercase hex
};

// ---- tiny update window ----
static HWND	g_updBar = NULL, g_updWnd = NULL;

static void UpdPump() {
	MSG msg;
	while ( PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ) ) {
		TranslateMessage( &msg );
		DispatchMessageW( &msg );
	}
}

static void UpdShow( const wchar_t *text ) {
	INITCOMMONCONTROLSEX icc = { sizeof( icc ), ICC_PROGRESS_CLASS };
	InitCommonControlsEx( &icc );
	WNDCLASSW wc = { 0 };
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW( NULL );
	wc.lpszClassName = L"JACoopUpd";
	wc.hCursor = LoadCursorW( NULL, IDC_ARROW );
	wc.hbrBackground = (HBRUSH)( COLOR_BTNFACE + 1 );
	RegisterClassW( &wc );
	g_updWnd = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"JACoopUpd", L"JACoop",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 380, 120,
		NULL, NULL, wc.hInstance, NULL );
	HWND lbl = CreateWindowExW( 0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
		16, 14, 340, 20, g_updWnd, NULL, wc.hInstance, NULL );
	(void)lbl;
	g_updBar = CreateWindowExW( 0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE,
		16, 42, 340, 18, g_updWnd, NULL, wc.hInstance, NULL );
	SendMessageW( g_updBar, PBM_SETRANGE32, 0, 1000 );
	ShowWindow( g_updWnd, SW_SHOW );
	UpdPump();
}

static void UpdProgress( DWORD done, DWORD total ) {
	if ( !g_updBar ) return;
	SendMessageW( g_updBar, PBM_SETPOS, (WPARAM)( total ? ( done * 1000 / total ) : 0 ), 0 );
	UpdPump();
}

static void UpdClose() {
	if ( g_updWnd ) { DestroyWindow( g_updWnd ); g_updWnd = NULL; }
	g_updBar = NULL;
}

// ---- HTTP: WinHTTP GET, into memory (manifest) or a file (payloads) ----
static bool HttpGet( const wchar_t *path, std::vector<BYTE> &out ) {
	HINTERNET session = NULL, connect = NULL, request = NULL;
	bool ok = false;
	session = WinHttpOpen( L"JACoop-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( !session ) goto done;
	connect = WinHttpConnect( session, UPD_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0 );
	if ( !connect ) goto done;
	request = WinHttpOpenRequest( connect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
	if ( !request ) goto done;
	WinHttpSetTimeouts( request, 4000, 4000, 4000, 8000 );
	if ( !WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ) goto done;
	if ( !WinHttpReceiveResponse( request, NULL ) ) goto done;
	{
		DWORD status = 0, sz = sizeof( status );
		WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX );
		if ( status != 200 ) goto done;
	}
	for ( ;; ) {
		BYTE buf[8192];
		DWORD avail = 0, got = 0;
		if ( !WinHttpQueryDataAvailable( request, &avail ) ) goto done;
		if ( !avail ) break;
		if ( avail > sizeof( buf ) ) avail = sizeof( buf );
		if ( !WinHttpReadData( request, buf, avail, &got ) || !got ) break;
		out.insert( out.end(), buf, buf + got );
	}
	ok = true;
done:
	if ( request ) WinHttpCloseHandle( request );
	if ( connect ) WinHttpCloseHandle( connect );
	if ( session ) WinHttpCloseHandle( session );
	return ok;
}

static bool HttpGetFile( const wchar_t *path, const std::wstring &dst,
		DWORD expectSize, DWORD base, DWORD total ) {
	HINTERNET session = NULL, connect = NULL, request = NULL;
	HANDLE out = INVALID_HANDLE_VALUE;
	bool ok = false;
	DWORD got = 0;
	session = WinHttpOpen( L"JACoop-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if ( !session ) goto done;
	connect = WinHttpConnect( session, UPD_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0 );
	if ( !connect ) goto done;
	request = WinHttpOpenRequest( connect, L"GET", path, NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
	if ( !request ) goto done;
	WinHttpSetTimeouts( request, 8000, 8000, 8000, 30000 );
	if ( !WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ) goto done;
	if ( !WinHttpReceiveResponse( request, NULL ) ) goto done;
	{
		DWORD status = 0, sz = sizeof( status );
		WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX );
		if ( status != 200 ) goto done;
	}
	out = CreateFileW( dst.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL );
	if ( out == INVALID_HANDLE_VALUE ) goto done;
	for ( ;; ) {
		BYTE buf[16384];
		DWORD avail = 0, rd = 0, wr = 0;
		if ( !WinHttpQueryDataAvailable( request, &avail ) ) break;
		if ( !avail ) break;
		if ( avail > sizeof( buf ) ) avail = sizeof( buf );
		if ( !WinHttpReadData( request, buf, avail, &rd ) || !rd ) break;
		if ( !WriteFile( out, buf, rd, &wr, NULL ) || wr != rd ) goto done;
		got += rd;
		UpdProgress( base + got, total );
	}
	ok = ( got == expectSize );
done:
	if ( out != INVALID_HANDLE_VALUE ) CloseHandle( out );
	if ( request ) WinHttpCloseHandle( request );
	if ( connect ) WinHttpCloseHandle( connect );
	if ( session ) WinHttpCloseHandle( session );
	if ( !ok ) DeleteFileW( dst.c_str() );
	return ok;
}

// ---- MD5 (CNG) for verify-after-download ----
static bool Md5File( const std::wstring &path, char out[33] ) {
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE h = NULL;
	bool ok = false;
	DWORD objLen = 0, cbData = 0, hashLen = 0;
	std::vector<BYTE> obj, hash;
	HANDLE f = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( f == INVALID_HANDLE_VALUE ) return false;
	if ( BCryptOpenAlgorithmProvider( &alg, BCRYPT_MD5_ALGORITHM, NULL, 0 ) != 0 ) goto done;
	BCryptGetProperty( alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof( objLen ), &cbData, 0 );
	BCryptGetProperty( alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof( hashLen ), &cbData, 0 );
	obj.resize( objLen ); hash.resize( hashLen );
	if ( BCryptCreateHash( alg, &h, obj.data(), objLen, NULL, 0, 0 ) != 0 ) goto done;
	for ( ;; ) {
		BYTE buf[65536];
		DWORD rd = 0;
		if ( !ReadFile( f, buf, sizeof( buf ), &rd, NULL ) ) goto done;
		if ( !rd ) break;
		if ( BCryptHashData( h, buf, rd, 0 ) != 0 ) goto done;
	}
	if ( BCryptFinishHash( h, hash.data(), hashLen, 0 ) != 0 ) goto done;
	for ( DWORD i = 0; i < hashLen && i < 16; i++ )
		sprintf( out + i * 2, "%02x", hash[i] );
	out[32] = 0;
	ok = true;
done:
	CloseHandle( f );
	if ( h ) BCryptDestroyHash( h );
	if ( alg ) BCryptCloseAlgorithmProvider( alg, 0 );
	return ok;
}

// ---- manifest: fixed shape, strstr parse ----
// {"version":"8adbc309","files":[{"name":"x","size":N,"md5":"h"},...]}
static bool ParseManifest( const std::string &json, std::string &ver, std::vector<UpdFile> &files ) {
	const char *p = strstr( json.c_str(), "\"version\":\"" );
	if ( !p ) return false;
	p += 11;
	const char *e = strchr( p, '"' );
	if ( !e || e - p > 80 ) return false;
	ver.assign( p, e - p );
	for ( p = strstr( p, "\"name\":\"" ); p; p = strstr( e, "\"name\":\"" ) ) {
		UpdFile f = { "", 0, "" };
		p += 8;
		e = strchr( p, '"' );
		if ( !e || e - p > 64 ) return false;
		f.name.assign( p, e - p );
		const char *s = strstr( e, "\"size\":" );
		const char *m = strstr( e, "\"md5\":\"" );
		if ( !s || !m ) return false;
		f.size = (DWORD)strtoul( s + 7, NULL, 10 );
		m += 7;
		const char *me = strchr( m, '"' );
		if ( !me || me - m != 32 ) return false;
		memcpy( f.md5, m, 32 );
		files.push_back( f );
		p = me;
	}
	return !files.empty();
}

// ---- JACoop.exe.new -> swapper: waits for this process to exit, replaces
// the launcher, relaunches it with the same mode/address ----
static void SpawnLauncherSwap( const std::wstring &mode, const std::wstring &address ) {
	std::wstring newExe = Join( g_root, L"JACoop.exe.new" );
	std::wstring exe = Join( g_root, L"JACoop.exe" );
	std::wstring cmd = L"cmd.exe /c ping -n 2 127.0.0.1 >nul && move /y \"" + newExe +
		L"\" \"" + exe + L"\" && start \"\" \"" + exe + L"\" " + mode;
	if ( !address.empty() ) cmd += L" " + address;
	std::vector<wchar_t> buf( cmd.begin(), cmd.end() );
	buf.push_back( 0 );
	STARTUPINFOW si = { sizeof( si ) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = { 0 };
	if ( CreateProcessW( NULL, buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW,
			NULL, NULL, &si, &pi ) ) {
		CloseHandle( pi.hThread );
		CloseHandle( pi.hProcess );
	}
}

// one instance at a time so two simultaneous launches don't corrupt the swap
static bool g_updMutexHeld = false;

static void SelfUpdate( const std::wstring &mode, const std::wstring &address ) {
	HANDLE mutex = CreateMutexW( NULL, TRUE, L"JACoopSelfUpdate" );
	if ( mutex && GetLastError() == ERROR_ALREADY_EXISTS ) {
		CloseHandle( mutex );
		return;	// another JACoop.exe is already updating — just launch
	}
	g_updMutexHeld = ( mutex != NULL );

	std::vector<BYTE> raw;
	if ( !HttpGet( L"/coop/manifest.json?upd=1", raw ) ) return;
	std::string json( raw.begin(), raw.end() ), ver;
	std::vector<UpdFile> files;
	if ( !ParseManifest( json, ver, files ) ) return;

	// installed build id — written by the installer / previous updates
	char cur[128] = { 0 };
	{
		HANDLE h = CreateFileW( Join( g_root, L"jacoop-version.txt" ).c_str(), GENERIC_READ,
			FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( h != INVALID_HANDLE_VALUE ) {
			DWORD rd = 0;
			ReadFile( h, cur, sizeof( cur ) - 1, &rd, NULL );
			CloseHandle( h );
		}
	}
	std::string curVer( cur );
	while ( !curVer.empty() && ( curVer.back() == '\r' || curVer.back() == '\n' || curVer.back() == ' ' ) )
		curVer.pop_back();
	if ( curVer == ver ) return;	// up to date — nothing to do

	// which files actually differ on disk? (md5 per target; root+bin copies
	// share one download — a missing/mismatched target alone triggers a fetch)
	struct Job {
		const UpdFile	*f;
		std::wstring	newPath;	// <root>\<name>.new staging
		std::vector<std::wstring>	targets;
	};
	std::vector<Job> jobs;
	DWORD totalBytes = 0;
	for ( const UpdFile &f : files ) {
		std::wstring wname( f.name.begin(), f.name.end() );
		Job j = { &f, Join( g_root, wname + L".new" ), {} };
		if ( f.name == "zz_jacoop.pk3" ) {
			j.targets.push_back( Join( g_root, L"base\\zz_jacoop.pk3" ) );
		} else if ( f.name == "JACoop.exe" ) {
			// self — staged only; SpawnLauncherSwap applies it after exit
		} else {
			j.targets.push_back( Join( g_root, wname ) );
			j.targets.push_back( Join( g_root, L"bin\\" + wname ) );
		}
		bool needs = false;
		char md5[33];
		if ( f.name == "JACoop.exe" ) {
			needs = !( Md5File( Join( g_root, L"JACoop.exe" ), md5 ) && !_stricmp( md5, f.md5 ) );
		} else {
			for ( const std::wstring &t : j.targets ) {
				if ( !( Md5File( t, md5 ) && !_stricmp( md5, f.md5 ) ) ) { needs = true; break; }
			}
		}
		if ( needs ) { jobs.push_back( j ); totalBytes += f.size; }
	}
	if ( jobs.empty() ) {
		// version bumped but bytes already match (e.g. local dev build)
		HANDLE h = CreateFileW( Join( g_root, L"jacoop-version.txt" ).c_str(), GENERIC_WRITE,
			0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( h != INVALID_HANDLE_VALUE ) {
			DWORD wr;
			WriteFile( h, ver.c_str(), (DWORD)ver.size(), &wr, NULL );
			WriteFile( h, "\n", 1, &wr, NULL );
			CloseHandle( h );
		}
		return;
	}

	UpdShow( Tr( L"Mise à jour de JACoop…", L"Updating JACoop…" ) );
	bool ok = true;
	DWORD done = 0;
	for ( Job &j : jobs ) {
		std::wstring wpath = L"/coop/files/" + std::wstring( j.f->name.begin(), j.f->name.end() );
		if ( !HttpGetFile( wpath.c_str(), j.newPath, j.f->size, done, totalBytes ) ) { ok = false; break; }
		char md5[33];
		if ( !Md5File( j.newPath, md5 ) || _stricmp( md5, j.f->md5 ) ) { ok = false; break; }
		done += j.f->size;
	}
	if ( ok ) {
		for ( Job &j : jobs ) {
			for ( const std::wstring &t : j.targets ) {
				CreateDirectoryW( t.substr( 0, t.find_last_of( L"\\/" ) ).c_str(), NULL );
				CopyFileW( j.newPath.c_str(), t.c_str(), FALSE );
			}
			if ( j.targets.empty() ) continue;	// JACoop.exe — stays *.new
			DeleteFileW( j.newPath.c_str() );	// engine files copied to all targets
		}
		HANDLE h = CreateFileW( Join( g_root, L"jacoop-version.txt" ).c_str(), GENERIC_WRITE,
			0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
		if ( h != INVALID_HANDLE_VALUE ) {
			DWORD wr;
			WriteFile( h, ver.c_str(), (DWORD)ver.size(), &wr, NULL );
			WriteFile( h, "\n", 1, &wr, NULL );
			CloseHandle( h );
		}
	}
	UpdClose();
	if ( ok && FileExists( Join( g_root, L"JACoop.exe.new" ) ) ) {
		SpawnLauncherSwap( mode, address );
		ExitProcess( 0 );
	}
}

int WINAPI wWinMain( HINSTANCE, HINSTANCE, LPWSTR, int ) {
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW( NULL, exePath, MAX_PATH );
	g_root = exePath;
	g_root = g_root.substr( 0, g_root.find_last_of( L"\\/" ) );
	g_ini = Join( g_root, L"jacoop.ini" );

	RegisterJoinProtocol( exePath );

	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW( GetCommandLineW(), &argc );
	std::wstring mode = argc > 1 ? argv[1] : L"host";
	std::wstring address = argc > 2 ? argv[2] : L"";
	for ( auto &c : mode ) c = (wchar_t)towlower( c );

	// advjk://join/<addr> or advjk://<addr> from the website
	if ( mode.rfind( L"advjk:", 0 ) == 0 ) {
		std::wstring u = mode;
		size_t p = u.find( L':' );
		u = u.substr( p + 1 );
		while ( !u.empty() && u.front() == L'/' ) u.erase( u.begin() );
		if ( u.rfind( L"join/", 0 ) == 0 ) u = u.substr( 5 );
		while ( !u.empty() && ( u.front() == L'/' || u.back() == L'/' ) ) {
			if ( u.front() == L'/' ) u.erase( u.begin() );
			else u.pop_back();
		}
		mode = L"join";
		address = u;
	}
	address = SanitizeAddress( address );

	// a previous update staged a newer launcher — apply it and relaunch
	if ( FileExists( Join( g_root, L"JACoop.exe.new" ) ) ) {
		SpawnLauncherSwap( mode, address );
		return 0;
	}
	SelfUpdate( mode, address );

	std::wstring gameData = FindGameData();
	if ( gameData.empty() ) return 1;
	MoveOldSaves();

	std::wstring exe = Join( g_root, L"bin\\openjk_sp.x86_64.exe" );
	if ( !FileExists( exe ) ) {
		MessageBoxW( NULL, ( std::wstring( Tr( L"Moteur introuvable :\n", L"Engine not found:\n" ) ) + exe ).c_str(), L"JACoop", MB_OK | MB_ICONERROR );
		return 1;
	}

	std::wstring cmd = L"\"" + exe + L"\" +set fs_basepath \"" + gameData + L"\" +set fs_homepath \"" + g_root + L"\" +exec jacoop.cfg";
	if ( mode == L"solo" ) {
		cmd += L" +set net_enabled 0 +set ui_coopJoin \"\"";
	} else if ( mode == L"join" ) {
		cmd += L" +set net_enabled 1 +set cl_timeout 120";
		if ( !address.empty() ) {
			IniSet( L"LastAddress", address );
			cmd += L" +set ui_coopJoin " + address;
		}
	} else {
		cmd += L" +set net_enabled 1 +set net_port 29070 +set sv_maxclients 4 +set ui_coopJoin \"\"";
	}

	STARTUPINFOW si = { sizeof( si ) };
	PROCESS_INFORMATION pi = { 0 };
	std::wstring cwd = Join( g_root, L"bin" );
	std::vector<wchar_t> cmdBuf( cmd.begin(), cmd.end() );
	cmdBuf.push_back( 0 );
	if ( !CreateProcessW( exe.c_str(), cmdBuf.data(), NULL, NULL, FALSE, 0, NULL, cwd.c_str(), &si, &pi ) ) {
		wchar_t msg[512];
		swprintf( msg, 512, Tr( L"Impossible de lancer le jeu (erreur %lu) :\n%s", L"Could not start the game (error %lu):\n%s" ), GetLastError(), exe.c_str() );
		MessageBoxW( NULL, msg, L"JACoop", MB_OK | MB_ICONERROR );
		return 1;
	}
	CloseHandle( pi.hThread );
	CloseHandle( pi.hProcess );
	return 0;
}
