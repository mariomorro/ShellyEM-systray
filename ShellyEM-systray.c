// shellyem.c — Icona tray per Shelly EM
// Compilazione MinGW:
//   gcc shellyem.c -o shellyem.exe -O2 -mwindows -D__USE_MINGW_ANSI_STDIO=1 -static -lgdiplus -lwinhttp -lshell32 -lole32 -lgdi32

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INTERVALLO_LETTURA 3
#define LATO 16

/* ================================================================= */
/*                    AREA DI CONFIGURAZIONE                         */
/*   Modifica questi valori, ricompila e guarda il risultato!        */
/* ================================================================= */


// 1. NOME DEL FONT (Deve essere un font installato in Windows)
// Opzioni consigliate: L"Arial Narrow", L"Segoe UI", L"Consolas", L"Tahoma", L"Verdana"
#define FONT_NAME L"Arial Narrow"

// 2. DIMENSIONE DEL FONT (Pixel)
// Nota: Arial Narrow sopporta 42, Segoe UI/Consolas/Tahoma è meglio 32-34
#define FONT_SIZE 10

// 3. STILE DEL FONT
// Opzioni: FontStyleRegular, FontStyleBold, FontStyleItalic
#define FONT_STYLE FontStyleRegular

// 4. SPAZIATURA VERTICALE (Distanza tra le due righe)
// Aumenta il valore se le scritte si toccano, diminuiscilo se sono troppo distanti
#define LINE_SPACING 4.3f 

// 5. RENDERING DEL TESTO (Qualità visiva)
// - TextRenderingHintAntiAliasGridFit -> Nitido, perfetto per l'UI di Windows
// - TextRenderingHintAntiAlias         -> Morbido, più simile a Python/Pillow
#define TEXT_RENDER_HINT TextRenderingHintAntiAliasGridFit

/* ================================================================= */


#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY     1
#define ID_TIMER    1

#define ID_MENU_CLOUD 1001
#define ID_MENU_LOCAL 1002
#define ID_MENU_EXIT  1003

static const wchar_t* SHELLY_URL        = L"http://192.168.1.11/status";
static const wchar_t* SHELLY_LOCALE_URL = L"http://192.168.1.11";
static const wchar_t* SHELLY_CLOUD_URL  = L"https://control.shelly.cloud/#/home/room/1/devices";

#define COLORE_CH1_NEG ((ARGB)0xFF32E678)   // Verde
#define COLORE_CH1_POS ((ARGB)0xFFFF8A80)   // Rosso corallo chiaro
#define COLORE_CH2     ((ARGB)0xFFFFD700)   // Oro
#define COLORE_ERR     ((ARGB)0xFFFF5050)   // Rosso

static HICON           g_icon          = NULL;
static NOTIFYICONDATAW g_nid           = {0};
static HWND            g_hwnd          = NULL;
static ULONG_PTR       g_gdiplusToken  = 0;

/* ---------------------------------------------------------------- */
/* Formattazione numero                                             */
/* ---------------------------------------------------------------- */
static void formatta(double valore, int segno, wchar_t* buf, size_t bufsz)
{
    int v = (int)lround(fabs(valore));
    if (v < 1000) {
        if (segno && valore < 0) swprintf(buf, bufsz, L"-%d", v);
        else                     swprintf(buf, bufsz, L"%d",  v);
    } else {
        double k = round(fabs(valore) / 100.0) / 10.0;
        if (k > 9.9) k = 9.9;
        if (segno && valore < 0) swprintf(buf, bufsz, L"-%.1fK", k);
        else                     swprintf(buf, bufsz, L"%.1fK",  k);
    }
}

static ARGB colore_ch1(double v) { return v < 0 ? COLORE_CH1_NEG : COLORE_CH1_POS; }

/* ---------------------------------------------------------------- */
/* HTTP GET (WinHTTP)                                               */
/* ---------------------------------------------------------------- */
static int http_get(const wchar_t* url, char** out_data)
{
    *out_data = NULL;
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof(uc);

    wchar_t host[512] = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = 512;
    uc.lpszUrlPath       = path;
    uc.dwUrlPathLength   = 2048;

    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;

    HINTERNET session = WinHttpOpen(L"ShellyEM/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return 0;

    HINTERNET conn = WinHttpConnect(session, host, (INTERNET_PORT)uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return 0; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return 0; }

    WinHttpSetTimeouts(req, 3000, 3000, 3000, 3000);

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return 0;
    }

    size_t cap = 4096, total = 0;
    char* buf = (char*)malloc(cap + 1);
    if (!buf) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return 0;
    }

    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(req, buf + total, (DWORD)(cap - total), &read) || read == 0) break;
        total += read;
        if (total + 1024 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap + 1);
            if (!nb) { free(buf); WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return 0; }
            buf = nb;
        }
    }
    buf[total] = 0;

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    *out_data = buf;
    return 1;
}

/* ---------------------------------------------------------------- */
/* Parser JSON minimale                                             */
/* ---------------------------------------------------------------- */
static int parse_emeters(const char* json, double* p1, double* p2)
{
    const char* p = strstr(json, "\"emeters\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;

    int found = 0;
    while (found < 2) {
        const char* q = strstr(p, "\"power\"");
        if (!q) break;
        q += 7;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q != ':') break;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;

        char* end = NULL;
        double v = strtod(q, &end);
        if (end == q) break;

        if (found == 0) *p1 = v;
        else            *p2 = v;
        found++;
        p = end;
    }
    return found == 2;
}

static int leggi_shelly(double* ch1, double* ch2)
{
    char* json = NULL;
    if (!http_get(SHELLY_URL, &json)) return 0;
    int ok = parse_emeters(json, ch1, ch2);
    free(json);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Disegno (GDI+)                                                   */
/* ---------------------------------------------------------------- */

static void draw_text_centered(GpGraphics* g, GpFont* font, GpStringFormat* fmt,
                               const wchar_t* text, ARGB color, REAL center_y)
{
    if (!text || !*text) return;

    // Allineamento in alto a sinistra per il calcolo dei pixel
    GdipSetStringFormatAlign(fmt, StringAlignmentNear);
    GdipSetStringFormatLineAlign(fmt, StringAlignmentNear);
    
    // Misuriamo il testo
    GpRectF layout = { 0.0f, 0.0f, (REAL)LATO * 4.0f, (REAL)LATO * 4.0f };
    GpRectF bounds;
    INT cp = 0, lines = 0;
    GdipMeasureString(g, text, -1, font, &layout, fmt, &bounds, &cp, &lines);

    // Calcoliamo la posizione esatta.
    // Aggiungiamo +1.0f alla X per spostare a destra
    // Sottraiamo -1.0f alla Y per spostare in alto
    REAL x = ((REAL)LATO - bounds.Width) / 2.0f - bounds.X + 1.0f;
    REAL y = center_y - (bounds.Height / 2.0f) - bounds.Y - 1.0f;

    // IL SEGRETO È QUI: diamo a GDI+ una larghezza enorme (LATO * 4.0f)
    // così non taglierà mai le cifre finali, iniziando a disegnare dal punto X esatto.
    GpRectF draw_rect = { x, y, (REAL)LATO * 4.0f, (REAL)LATO * 4.0f };
    
    GpSolidFill* brush = NULL;
    GdipCreateSolidFill(color, &brush);
    GdipDrawString(g, text, -1, font, &draw_rect, fmt, brush);
    GdipDeleteBrush(brush);
}


static HICON crea_icona(const wchar_t* testo_alto,  ARGB colore_alto,
                        const wchar_t* testo_basso, ARGB colore_basso)
{
    GpBitmap* bitmap = NULL;
    if (GdipCreateBitmapFromScan0(LATO, LATO, LATO * 4, PixelFormat32bppARGB, NULL, &bitmap) != Ok)
        return NULL;

    GpGraphics* g = NULL;
    GdipGetImageGraphicsContext(bitmap, &g);
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    
    // Usa l'impostazione definita in cima
    GdipSetTextRenderingHint(g, TEXT_RENDER_HINT); 
    
    // =========================================================
    // MODIFICA PER VEDERE LO SFONDO E LA DIMENSIONE
    // 0xFFFF00FF = Magenta (ottimo per il debug)
    // 0xFF333333 = Grigio scuro (più elegante)
    // 0x00000000 = Trasparente (come era prima)
    GdipGraphicsClear(g, 0x00000000); 
    // =========================================================

    GpFontFamily* family = NULL;
    // Usa il nome del font definito in cima
    GdipCreateFontFamilyFromName(FONT_NAME, NULL, &family);
    if (!family) {
        // Fallback se il font non viene trovato
        GdipCreateFontFamilyFromName(L"Arial", NULL, &family);
    }

    GpFont* font = NULL;
    // Usa dimensione e stile definiti in cima
    GdipCreateFont(family, FONT_SIZE, FONT_STYLE, UnitPixel, &font);

    GpStringFormat* fmt = NULL;
    GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt);
    GdipSetStringFormatAlign(fmt, StringAlignmentCenter);
    GdipSetStringFormatLineAlign(fmt, StringAlignmentCenter); 
    GdipSetStringFormatFlags(fmt, StringFormatFlagsNoWrap | StringFormatFlagsNoClip);

    // Usa la spaziatura definita in cima
    draw_text_centered(g, font, fmt, testo_alto,  colore_alto,  LATO / 2.0f - LINE_SPACING);
    draw_text_centered(g, font, fmt, testo_basso, colore_basso, LATO / 2.0f + LINE_SPACING);

    HICON hIcon = NULL;
    GdipCreateHICONFromBitmap(bitmap, &hIcon);

    GdipDeleteStringFormat(fmt);
    GdipDeleteFont(font);
    GdipDeleteFontFamily(family);
    GdipDeleteGraphics(g);
    GdipDisposeImage((GpImage*)bitmap);

    return hIcon;

}

/* ---------------------------------------------------------------- */
/* Aggiornamento icona                                              */
/* ---------------------------------------------------------------- */
static void aggiorna_icona(void)
{
    double ch1 = 0, ch2 = 0;
    HICON new_icon = NULL;
    wchar_t tooltip[256];

    if (leggi_shelly(&ch1, &ch2)) {
        wchar_t t1[32], t2[32];
        formatta(ch1, 0, t1, 32);
        formatta(ch2, 1, t2, 32);
        new_icon = crea_icona(t1, colore_ch1(ch1), t2, COLORE_CH2);
        swprintf(tooltip, 256, L"CH1: %d W | CH2: %d W", (int)lround(ch1), (int)lround(ch2));
    } else {
        new_icon = crea_icona(L"ERR", COLORE_ERR, L"", COLORE_ERR);
        swprintf(tooltip, 256, L"Errore di connessione a Shelly");
    }

    if (!new_icon) return;

    HICON old = g_icon;
    g_icon      = new_icon;
    g_nid.hIcon = new_icon;
    wcsncpy(g_nid.szTip, tooltip, 127);
    g_nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

/* ---------------------------------------------------------------- */
/* Window procedure                                                 */
/* ---------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd             = hwnd;
        g_nid.uID              = ID_TRAY;
        g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon            = crea_icona(L"...", COLORE_CH1_POS, L"...", COLORE_CH2);
        g_icon = g_nid.hIcon;
        wcsncpy(g_nid.szTip, L"Avvio lettura Shelly...", 127);
        g_nid.szTip[127] = 0;
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        SetTimer(hwnd, ID_TIMER, INTERVALLO_LETTURA * 1000, NULL);
        PostMessageW(hwnd, WM_TIMER, ID_TIMER, 0);
        return 0;

    case WM_TIMER:
        if (wp == ID_TIMER) aggiorna_icona();
        return 0;

    case WM_TRAYICON: {
        UINT event = LOWORD(lp);
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_MENU_CLOUD, L"Apri Shelly Cloud");
            AppendMenuW(menu, MF_STRING, ID_MENU_LOCAL, L"Apri Shelly (192.168.1.11)");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_MENU_EXIT, L"Esci");

            SetForegroundWindow(hwnd);
            int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);

            if (cmd == ID_MENU_CLOUD) {
                ShellExecuteW(NULL, L"open", SHELLY_CLOUD_URL,  NULL, NULL, SW_SHOWNORMAL);
            } else if (cmd == ID_MENU_LOCAL) {
                ShellExecuteW(NULL, L"open", SHELLY_LOCALE_URL, NULL, NULL, SW_SHOWNORMAL);
            } else if (cmd == ID_MENU_EXIT) {
                DestroyWindow(hwnd);
            }
        } else if (event == WM_LBUTTONDBLCLK) {
            ShellExecuteW(NULL, L"open", SHELLY_CLOUD_URL, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_icon) { DestroyIcon(g_icon); g_icon = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------- */
/* Entry point                                                      */
/* ---------------------------------------------------------------- */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int nShow)
{
    (void)hPrev; (void)cmd; (void)nShow;

    GdiplusStartupInput gsi = {0};
    gsi.GdiplusVersion = 1;
    if (GdiplusStartup(&g_gdiplusToken, &gsi, NULL) != Ok)
        return 1;

    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"ShellyEMClass";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(L"ShellyEMClass", L"ShellyEM", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           NULL, NULL, hInst, NULL);
    if (!g_hwnd) {
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    return 0;
}