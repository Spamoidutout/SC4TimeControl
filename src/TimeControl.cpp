// TimeControl.cpp - SC4 Time Control.
//
// - Right click on the "God mode" button: a small Maxis-style menu with the 24
//   hours in two columns and a row of cycle speeds (Pause, 1/2, x1, x2, x4, x8).
//   An hour issues the game's own cheat ("whattimeizit <hour>"); a speed sets
//   the magnification of the city's 24-hour clock (the game's normal is 135).
// - The time of day is shown next to the date, in a widened date field.
// - SC4TimeControl.ini: speed applied when a city is loaded, and whether the
//   last speed picked becomes that default.
//
// No heap, no CRT, no STL. Rules: subscribe once in PostAppInit; the mouse
// message only reads and notes; everything else happens in the OnIdle of a
// system service.

#include "FixedString.h"

#include "cIGZApp.h"
#include "cIGZCheatCodeManager.h"
#include "cIGZCOM.h"
#include "cIGZCOMDirector.h"
#include "cIGZFrameWork.h"
#include "cIGZFrameWorkHooks.h"
#include "cIGZMessage2.h"
#include "cIGZMessageServer2.h"
#include "cIGZMessageTarget2.h"
#include "cIGZSystemService.h"
#include "cIGZWin.h"
#include "cIGZWinGen.h"
#include "cIGZWinText.h"
#include "cISC424HourClock.h"
#include "cISC4App.h"
#include "cISC4City.h"

// The INI file (kernel32), declared by hand: no windows.h.
extern "C"
{
	__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* module, char* path, unsigned long size);
	__declspec(dllimport) unsigned long __stdcall GetPrivateProfileStringA(const char* section, const char* key, const char* def,
	                                                                       char* out, unsigned long size, const char* file);
	__declspec(dllimport) unsigned int __stdcall GetPrivateProfileIntA(const char* section, const char* key, int def, const char* file);
	__declspec(dllimport) int __stdcall WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file);
	extern char __ImageBase;   // this DLL's module handle (set by the linker)
}

namespace
{
	// ----- our ids (the .dat ones must match tools/build_dat.py) -----
	constexpr uint32_t kUIGroup        = 0x892F59E5;   // UI scripts: {type 0, kUIGroup, instance}
	constexpr uint32_t kMenu           = 0x00000001;   // instance of the menu
	constexpr uint32_t kMenuRoot       = 0x857A0EB3;
	constexpr uint32_t kClock          = 0x00000002;   // instance of the clock
	constexpr uint32_t kClockRoot      = 0x3D6A3173;
	constexpr uint32_t kClockText      = 0x21;
	constexpr uint32_t kTitle          = 0x10;
	constexpr uint32_t kSpeedTitle     = 0x11;
	constexpr uint32_t kHourTextFirst  = 0x100;        // + hour
	constexpr uint32_t kHourHitFirst   = 0x200;        // + hour
	constexpr uint32_t kSpeedTextFirst = 0x300;        // + speed index
	constexpr uint32_t kSpeedHitFirst  = 0x310;        // + speed index
	constexpr uint32_t kDirectorID     = 0xAE612FF7;
	constexpr uint32_t kServiceID      = 0x3830B6EB;

	const char kTitleText[] = "Time of Day";
	const char kSpeedText[] = "Cycle Speed";
	const char kCheat[]     = "whattimeizit";
	const char kIniSection[] = "TimeControl";

	// Speed row: index 0 = pause, then clock magnifications (game normal = 135).
	constexpr int32_t kSpeeds = 6;
	constexpr int32_t kPause = 0, kNormalSpeed = 2;
	const char* const kSpeedLabels[kSpeeds] = { "Pause", "1/2", "x1", "x2", "x4", "x8" };
	const char* const kSpeedAliases[kSpeeds] = { "", "0.5", "1", "2", "4", "8" };   // also accepted in the INI
	constexpr float kSpeedValues[kSpeeds] = { 0.0f, 67.5f, 135.0f, 270.0f, 540.0f, 1080.0f };
	constexpr int32_t kSpeedItem = 100;              // menu items: hours 0..23, speeds 100 + index

	// The clock's place in the game's bottom-left panel, measured on 1:1
	// screenshots (panel at (5,868)): piece at screen (155,996), time text at
	// (154,1008) like the date text (103,1008, 15 px high).
	constexpr int32_t kClockX = 149, kClockY = 128;

	// ----- game ids -----
	constexpr uint32_t kGodModeButton            = 0x2988BC85;
	constexpr uint32_t kControlPanel             = 0x0987B48F;   // bottom-left panel, holds the date
	constexpr uint32_t kGZWin_WinSC4App          = 0x6104489A;   // parent of the game's UI
	constexpr uint32_t kMouseMessage             = 0xFA0C6390;
	constexpr uint32_t kGZIID_cIGZMouseMessage   = 0x1A0C615F;
	constexpr uint32_t kLeftDown = 7, kLeftUp = 10, kRightUp = 11;
	constexpr uint32_t kUIScriptServiceID        = 0x5A356E15;
	constexpr uint32_t kGZIID_cIGZUIScriptService = 0xFA3562FA;
	constexpr uint32_t kGZIID_cIGZWinGen         = 0x5386D516;
	constexpr uint32_t kGZIID_cIGZWinText        = 0x212CDC1F;
	constexpr uint32_t kMessageServer2ServiceID  = 0x04FA845B;
	constexpr uint32_t kGZIID_cIGZMessageServer2 = 0x652294C7;
	constexpr uint32_t kGZIID_cIGZCOMDirector    = 0xA21EE941;
	constexpr uint32_t kGZIID_cIGZFrameWorkHooks = 0x03FA40BF;

	constexpr uint32_t kTextColor   = 0xFF3F4967;   // dark blue of the interface texts
	constexpr uint32_t kActiveColor = 0xFFB00101;   // red, readable on the light blue
	constexpr uint32_t kClockColor  = 0xFF344566;   // colour of the game's date

	// The SDK headers for these pull in the STL: declared by hand.
	struct UIResKey { uint32_t type, group, instance; };
	class cIGZUIScriptServiceMin : public cIGZUnknown
	{
	public:
		virtual bool CreateWindowFromScript(UIResKey const& key, cIGZWin* parent, uint32_t rootID, uint32_t riid, void** ppvObj) = 0; // slot 3
	};

	struct Point { int32_t x, y; };

	// ======================================================================
	// Helpers
	// ======================================================================

	// Mouse message: slot 9 = event, slot 10 = screen position (hidden pointer).
	bool ReadMouse(cIGZMessage2* msg, uint32_t& event, Point& pt)
	{
		void* p = nullptr;
		if (!msg->QueryInterface(kGZIID_cIGZMouseMessage, &p) || !p) return false;
		void** vt = *reinterpret_cast<void***>(p);
		event = reinterpret_cast<uint32_t(__thiscall*)(void*)>(vt[9])(p);
		pt.x = pt.y = 0;
		reinterpret_cast<Point*(__thiscall*)(void*, Point*)>(vt[10])(p, &pt);
		static_cast<cIGZUnknown*>(p)->Release();
		return true;
	}

	// Windows only know their position inside their parent.
	void AbsolutePos(cIGZWin* w, int32_t& x, int32_t& y)
	{
		x = y = 0;
		for (int depth = 0; w && depth < 16; ++depth)
		{
			x += w->GetL();
			y += w->GetT();
			w = w->GetParentWin();
		}
	}

	bool Inside(cIGZWin* w, int32_t x, int32_t y)
	{
		if (!w) return false;
		int32_t ax, ay;
		AbsolutePos(w, ax, ay);
		return x >= ax && x < ax + w->GetW() && y >= ay && y < ay + w->GetH();
	}

	// Still in the game's window tree? (a reference we hold keeps the object alive)
	bool Attached(cIGZWin* w, cIGZWin* main)
	{
		for (int depth = 0; w && depth < 16; ++depth)
		{
			if (w == main) return true;
			w = w->GetParentWin();
		}
		return false;
	}

	bool SameText(const char* a, const char* b)   // ASCII, case-insensitive
	{
		for (;; ++a, ++b)
		{
			const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
			const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
			if (x != y) return false;
			if (!x) return true;
		}
	}

	// ======================================================================
	// State
	// ======================================================================
	cIGZFrameWork* gFW = nullptr;
	cISC4App* gApp = nullptr;
	bool gStopping = false;

	// Settings (SC4TimeControl.ini, next to the DLL)
	char gIniPath[300];
	int32_t gDefaultSpeed = kNormalSpeed;
	bool gRememberSpeed = false;

	// The menu, when open
	cIGZWinGen* gMenuGen = nullptr;
	cIGZWin* gMenuWin = nullptr;

	// Noted by the mouse message, handled in OnIdle
	bool gOpen = false, gClose = false;
	int32_t gPicked = -1, gPressed = -1;            // menu items
	int32_t gBx = 0, gBy = 0, gBw = 0, gBh = 0;   // God mode button, screen rectangle

	// The clock next to the date
	cIGZWin* gPanel = nullptr;        // the game's panel (one reference held)
	cIGZWinGen* gClockGen = nullptr;
	cIGZWin* gClockWin = nullptr;
	int32_t gClockShown = -1;         // hour * 60 + minute on screen

	// The city's 24-hour clock
	cISC424HourClock* gCityClock = nullptr;     // the clock the default speed was applied to
	cISC424HourClock* gPausedClock = nullptr;   // the clock WE paused (one Pause(true) to undo)

	constinit FixedString gText;        // captions
	constinit FixedString gCheatLine;   // "whattimeizit 14", kept alive after the send
	uint32_t gCheatID = 0;

	// ======================================================================
	// Settings
	// ======================================================================
	void LoadSettings()
	{
		const unsigned long n = GetModuleFileNameA(&__ImageBase, gIniPath, sizeof(gIniPath));
		uint32_t dot = n;
		while (dot && gIniPath[dot - 1] != '.' && gIniPath[dot - 1] != '\\') --dot;
		if (n == 0 || n >= sizeof(gIniPath) - 4 || !dot || gIniPath[dot - 1] != '.') { gIniPath[0] = 0; return; }
		gIniPath[dot] = 'i'; gIniPath[dot + 1] = 'n'; gIniPath[dot + 2] = 'i'; gIniPath[dot + 3] = 0;

		char value[16];
		GetPrivateProfileStringA(kIniSection, "DefaultSpeed", "x1", value, sizeof(value), gIniPath);
		for (int32_t i = 1; i < kSpeeds; ++i)
			if (SameText(value, kSpeedLabels[i]) || SameText(value, kSpeedAliases[i])) gDefaultSpeed = i;
		gRememberSpeed = GetPrivateProfileIntA(kIniSection, "RememberLastSpeed", 0, gIniPath) != 0;
	}

	void RememberSpeed(int32_t index)
	{
		if (!gRememberSpeed || index == kPause || !gIniPath[0]) return;
		gDefaultSpeed = index;
		WritePrivateProfileStringA(kIniSection, "DefaultSpeed", kSpeedLabels[index], gIniPath);
	}

	// ======================================================================
	// From DoMessage: read and note only
	// ======================================================================
	int32_t ItemAt(int32_t x, int32_t y)
	{
		for (int32_t h = 0; h < 24; ++h)
			if (Inside(gMenuWin->GetChildWindowFromID(kHourHitFirst + h), x, y)) return h;
		for (int32_t i = 0; i < kSpeeds; ++i)
			if (Inside(gMenuWin->GetChildWindowFromID(kSpeedHitFirst + i), x, y)) return kSpeedItem + i;
		return -1;
	}

	void OnMouse(cIGZMessage2* msg)
	{
		uint32_t event = 0;
		Point pt;
		if (gStopping || !gApp || !ReadMouse(msg, event, pt)) return;

		if (event == kRightUp)
		{
			if (gMenuWin) { gClose = true; return; }   // any right click closes the menu

			cIGZWin* main = gApp->GetMainWindow();
			cIGZWin* b = main ? main->GetChildWindowFromIDRecursive(kGodModeButton) : nullptr;
			for (cIGZWin* p = b; p && p != main; p = p->GetParentWin())
				if (!p->IsVisible()) return;
			if (!Inside(b, pt.x, pt.y)) return;

			AbsolutePos(b, gBx, gBy);
			gBw = b->GetW();
			gBh = b->GetH();
			gOpen = true;
		}
		else if (gMenuWin && event == kLeftDown)
		{
			if (!Inside(gMenuWin, pt.x, pt.y)) gClose = true;
			else gPressed = ItemAt(pt.x, pt.y);
		}
		else if (gMenuWin && event == kLeftUp)
		{
			const int32_t item = ItemAt(pt.x, pt.y);
			if (item >= 0 && item == gPressed) gPicked = item;   // press and release on the same item
			gPressed = -1;
		}
	}

	// ======================================================================
	// From OnIdle: windows
	// ======================================================================

	// A window from one of our UI scripts, made a child of `parent`, without the keyboard focus.
	cIGZWinGen* CreateScriptWindow(uint32_t instance, uint32_t rootID, cIGZWin* parent)
	{
		cIGZUIScriptServiceMin* ui = nullptr;
		if (!parent || !gFW->GetSystemService(kUIScriptServiceID, kGZIID_cIGZUIScriptService, reinterpret_cast<void**>(&ui)) || !ui)
			return nullptr;
		UIResKey key = { 0, kUIGroup, instance };
		cIGZWinGen* g = nullptr;
		const bool ok = ui->CreateWindowFromScript(key, parent, rootID, kGZIID_cIGZWinGen, reinterpret_cast<void**>(&g)) && g;
		ui->Release();
		if (!ok) return nullptr;   // the .dat is not in Plugins

		cIGZWin* w = g->AsIGZWin();
		cIGZWin* p = w->GetParentWin();
		if (p != parent)
		{
			if (p) p->ChildRemove(w);
			parent->ChildAdd(w);
		}
		if (w->GetFlag(cIGZWin::WinFlag_AcceptFocus)) w->SetFlag(cIGZWin::WinFlag_AcceptFocus, false);
		return g;
	}

	void DestroyScriptWindow(cIGZWinGen*& gen, cIGZWin*& win)
	{
		if (win)
		{
			win->HideWindow();
			cIGZWin* p = win->GetParentWin();
			if (p) p->ChildRemove(win);
			gen->Release();
		}
		gen = nullptr;
		win = nullptr;
	}

	bool SetCaption(cIGZWin* w, const char* text, uint32_t color)
	{
		cIGZWinText* t = nullptr;
		if (!w || !w->QueryInterface(kGZIID_cIGZWinText, reinterpret_cast<void**>(&t)) || !t) return false;
		gText.FromChar(text);
		t->SetBackgroundOpaque(false);
		t->SetTextColor(color);
		t->SetCaption(gText);
		t->Release();
		return true;
	}

	cISC424HourClock* Clock()
	{
		cISC4City* city = gApp ? gApp->GetCity() : nullptr;
		return city ? city->Get24HourClock() : nullptr;
	}

	// ======================================================================
	// The menu
	// ======================================================================
	void SetMenuText(uint32_t id, const char* text, uint32_t color = kTextColor)
	{
		SetCaption(gMenuWin->GetChildWindowFromID(id), text, color);
	}

	void OpenMenu()
	{
		cIGZWin* main = gApp->GetMainWindow();
		cIGZWin* parent = main ? main->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
		if (!parent) parent = main;
		gMenuGen = CreateScriptWindow(kMenu, kMenuRoot, parent);
		if (!gMenuGen) return;
		gMenuWin = gMenuGen->AsIGZWin();

		SetMenuText(kTitle, kTitleText);
		SetMenuText(kSpeedTitle, kSpeedText);
		char label[6] = { '0', '0', ':', '0', '0', 0 };
		for (int32_t h = 0; h < 24; ++h)
		{
			label[0] = static_cast<char>('0' + h / 10);
			label[1] = static_cast<char>('0' + h % 10);
			SetMenuText(kHourTextFirst + h, label);
		}

		// Current speed in red: pause if the clock is paused, else the matching magnification.
		cISC424HourClock* k = Clock();
		int32_t active = -1;
		if (k)
		{
			float m = 0;
			k->GetTimeMagnification(m);
			if (k->IsPaused()) active = kPause;
			else
				for (int32_t i = 1; i < kSpeeds; ++i)
					if (m > kSpeedValues[i] - 0.5f && m < kSpeedValues[i] + 0.5f) active = i;
		}
		for (int32_t i = 0; i < kSpeeds; ++i)
			SetMenuText(kSpeedTextFirst + i, kSpeedLabels[i], i == active ? kActiveColor : kTextColor);

		// On the right of the button, centred on it vertically, kept on screen.
		int32_t px, py;
		AbsolutePos(parent, px, py);
		const int32_t w = gMenuWin->GetW(), h = gMenuWin->GetH();
		int32_t x = gBx + gBw + 6 - px;
		int32_t y = gBy + gBh / 2 - h / 2 - py;
		if (x > parent->GetW() - w) x = gBx - w - 6 - px;
		if (y > parent->GetH() - h) y = parent->GetH() - h;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		gMenuWin->GZWinMoveTo(x, y);
		gMenuWin->ShowWindow();
		gMenuWin->PullToFront();
		gPressed = gPicked = -1;
	}

	void CloseMenu()
	{
		DestroyScriptWindow(gMenuGen, gMenuWin);
		gPressed = gPicked = -1;
	}

	// Hour: "whattimeizit <hour>", as the Ctrl-X box does.
	void SetHour(int32_t hour)
	{
		cIGZCheatCodeManager* mgr = gApp->GetCheatCodeManager();
		if (!mgr) return;
		if (gCheatID == 0)
		{
			gText.FromChar(kCheat);
			if (!mgr->DoesCheatCodeMatch(gText, gCheatID)) { gCheatID = 0; return; }
		}
		char line[sizeof(kCheat) + 3];
		uint32_t n = 0;
		for (const char* s = kCheat; *s; ++s) line[n++] = *s;
		line[n++] = ' ';
		if (hour >= 10) line[n++] = static_cast<char>('0' + hour / 10);
		line[n++] = static_cast<char>('0' + hour % 10);
		line[n] = 0;
		gCheatLine.FromChar(line);
		mgr->SendCheatNotifications(gCheatLine, gCheatID);
	}

	// Speed. Pause() is a counter that the stopwatch cheat uses too: the pause
	// item only adds or removes OUR pause.
	void SetSpeed(cISC424HourClock* k, int32_t index)
	{
		if (index == kPause)
		{
			if (gPausedClock == k) { k->Pause(false); gPausedClock = nullptr; }
			else if (!k->IsPaused()) { k->Pause(true); gPausedClock = k; }
			return;
		}
		if (gPausedClock == k) { k->Pause(false); gPausedClock = nullptr; }
		k->SetTimeMagnification(kSpeedValues[index]);
	}

	// ======================================================================
	// The clock next to the date: our own window, child of the game's panel
	// (the widened right end of the date field, and a text for the time).
	// The game's date itself is not touched.
	// ======================================================================

	// The date text: in an id-0 child of the panel, id 1, the text with a '/'.
	struct DateSearch { cIGZWin* parent; cIGZWin* found; };

	bool FindDate(cIGZWin*, uint32_t, void* child, void* ctx)
	{
		DateSearch* s = static_cast<DateSearch*>(ctx);
		cIGZWin* c = static_cast<cIGZWin*>(child);
		if (s->found || !c || c->GetParentWin() != s->parent || c->GetID() != 0) return true;
		cIGZWin* t = c->GetChildWindowFromID(1);
		cIGZString* cap = t ? t->GetCaption() : nullptr;
		const char* p = cap ? cap->ToChar() : nullptr;
		while (p && *p && *p != '/') ++p;
		if (p && *p == '/') s->found = t;
		return true;
	}

	// Our time text gets the font style of the game's date text.
	void CopyDateFont(cIGZWin* panel, cIGZWin* text)
	{
		DateSearch s = { panel, nullptr };
		panel->EnumChildren(GZIID_cIGZWin, &FindDate, &s);
		cIGZWinText* from = nullptr;
		cIGZWinText* to = nullptr;
		if (s.found && s.found->QueryInterface(kGZIID_cIGZWinText, reinterpret_cast<void**>(&from)) && from)
		{
			uint32_t style = 0;
			if (from->GetFontStyle(style) && text && text->QueryInterface(kGZIID_cIGZWinText, reinterpret_cast<void**>(&to)) && to)
			{
				to->SetFontStyle(style);
				to->Release();
			}
			from->Release();
		}
	}

	void CloseClock()
	{
		DestroyScriptWindow(gClockGen, gClockWin);
		gClockShown = -1;
		if (gPanel) gPanel->Release();
		gPanel = nullptr;
	}

	bool OpenClock(cIGZWin* main)
	{
		cIGZWin* panel = main->GetChildWindowFromIDRecursive(kControlPanel);
		gClockGen = CreateScriptWindow(kClock, kClockRoot, panel);
		if (!gClockGen) return false;
		gClockWin = gClockGen->AsIGZWin();
		gPanel = panel;
		gPanel->AddRef();

		gClockWin->GZWinMoveTo(kClockX, kClockY);
		CopyDateFont(panel, gClockWin->GetChildWindowFromID(kClockText));
		gClockWin->ShowWindow();
		gClockWin->PullToFront();
		return true;
	}

	void UpdateClock(cISC424HourClock* k)
	{
		cIGZWin* main = gApp->GetMainWindow();
		if (!main) return;
		if (gPanel && !Attached(gPanel, main)) CloseClock();   // the panel was replaced
		if (!gClockWin && !OpenClock(main)) return;

		// Steps of 5 minutes, 10 at x8: the digits do not race.
		float m = 0;
		k->GetTimeMagnification(m);
		const int32_t step = m >= kSpeedValues[kSpeeds - 1] - 0.5f ? 10 : 5;
		const int32_t hour = static_cast<int32_t>(k->GetHour() % 24);
		const int32_t minute = static_cast<int32_t>(k->GetMinute() % 60) / step * step;
		if (hour * 60 + minute == gClockShown) return;
		gClockShown = hour * 60 + minute;
		const char text[6] = { static_cast<char>('0' + hour / 10), static_cast<char>('0' + hour % 10), ':',
		                       static_cast<char>('0' + minute / 10), static_cast<char>('0' + minute % 10), 0 };
		SetCaption(gClockWin->GetChildWindowFromID(kClockText), text, kClockColor);
	}

	// ======================================================================
	// OnIdle
	// ======================================================================
	void IdleWork()
	{
		if (gStopping) return;
		cISC424HourClock* k = Clock();

		if (!k)   // no city
		{
			CloseMenu();
			CloseClock();
			gCityClock = gPausedClock = nullptr;
			gOpen = gClose = false;
			return;
		}

		if (gMenuWin && (gClose || gPicked >= 0))
		{
			const int32_t item = gPicked;
			CloseMenu();
			if (item >= kSpeedItem)
			{
				SetSpeed(k, item - kSpeedItem);
				RememberSpeed(item - kSpeedItem);
			}
			else if (item >= 0) SetHour(item);
		}
		else if (!gMenuWin && gOpen)
		{
			OpenMenu();
		}
		gOpen = gClose = false;

		UpdateClock(k);

		// A newly loaded city: once its interface is up, apply the default speed.
		if (k != gCityClock && gClockWin)
		{
			gCityClock = k;
			gPausedClock = nullptr;
			SetSpeed(k, gDefaultSpeed);
		}
	}

	// ======================================================================
	// COM objects (constant-initialised statics: no global constructors)
	// ======================================================================
	class TimeControlService final : public cIGZSystemService
	{
	public:
		constexpr TimeControlService() : refs(0), id(kServiceID), running(false) {}

		bool QueryInterface(uint32_t riid, void** ppv) override
		{
			if (riid == GZIID_cIGZSystemService || riid == GZIID_cIGZUnknown)
			{
				*ppv = static_cast<cIGZSystemService*>(this);
				AddRef();
				return true;
			}
			*ppv = nullptr;
			return false;
		}
		uint32_t AddRef() override { return ++refs; }
		uint32_t Release() override { if (refs) --refs; return refs; }

		uint32_t GetServiceID() override { return id; }
		cIGZSystemService* SetServiceID(uint32_t v) override { id = v; return this; }
		int32_t GetServicePriority() override { return 1000000; }
		bool IsServiceRunning() override { return running; }
		cIGZSystemService* SetServiceRunning(bool r) override { running = r; return this; }
		bool Init() override { return true; }
		bool Shutdown() override { return true; }
		bool OnTick(uint32_t) override { return true; }
		bool OnIdle(uint32_t) override { IdleWork(); return true; }
		int32_t GetServiceTickPriority() override { return 1000000; }

	private:
		uint32_t refs;
		uint32_t id;
		bool running;
	};

	constinit TimeControlService gService;

	class TimeControlDirector final : public cIGZCOMDirector, public cIGZFrameWorkHooks, public cIGZMessageTarget2
	{
	public:
		constexpr TimeControlDirector() : refs(0), com(nullptr), fw(nullptr) {}

		// ---- cIGZUnknown ----
		bool QueryInterface(uint32_t riid, void** ppv) override
		{
			switch (riid)
			{
			case kGZIID_cIGZCOMDirector:    *ppv = static_cast<cIGZCOMDirector*>(this); break;
			case kGZIID_cIGZFrameWorkHooks: *ppv = static_cast<cIGZFrameWorkHooks*>(this); break;
			case GZIID_cIGZUnknown:         *ppv = static_cast<cIGZUnknown*>(static_cast<cIGZCOMDirector*>(this)); break;
			default: *ppv = nullptr; return false;
			}
			AddRef();
			return true;
		}
		uint32_t AddRef() override { return ++refs; }
		uint32_t Release() override { return RemoveRef(); }

		// ---- cIGZCOMDirector ----
		bool InitializeCOM(cIGZCOM* pCOM, const cIGZString&) override
		{
			if (!pCOM) return false;
			com = pCOM;
			fw = pCOM->FrameWork();
			return true;
		}
		bool OnStart(cIGZCOM*) override
		{
			if (fw) fw->AddHook(static_cast<cIGZFrameWorkHooks*>(this));
			return true;
		}
		void EnumClassObjects(ClassObjectEnumerationCallback, void*) override {}
		bool GetClassObject(uint32_t, uint32_t, void**) override { return false; }
		bool CanUnloadNow() override { return refs == 0; }
		bool OnUnload() override { return true; }
		uint32_t RefCount() override { return refs; }
		uint32_t RemoveRef() override { if (refs) --refs; return refs; }
		cIGZFrameWork* FrameWork() override { return fw; }
		cIGZCOM* GZCOM() override { return com; }
		uint32_t GetDirectorID() const override { return kDirectorID; }
		bool GetLibraryPath(cIGZString&) override { return false; }
		uint32_t GetHeapAllocatedSize() override { return 0; }

		// ---- cIGZFrameWorkHooks ----
		bool PreFrameWorkInit() override { return true; }
		bool PreAppInit() override { return true; }
		bool PostAppInit() override
		{
			gFW = fw;
			cIGZApp* app = fw ? fw->Application() : nullptr;
			if (!app || !app->QueryInterface(GZIID_cISC4App, reinterpret_cast<void**>(&gApp)) || !gApp) return true;
			LoadSettings();

			// Once for the whole session (never while the game dispatches a message).
			cIGZMessageServer2* ms = nullptr;
			if (fw->GetSystemService(kMessageServer2ServiceID, kGZIID_cIGZMessageServer2, reinterpret_cast<void**>(&ms)) && ms)
			{
				ms->AddNotification(static_cast<cIGZMessageTarget2*>(this), kMouseMessage);
				ms->Release();
			}
			fw->AddSystemService(&gService);
			fw->AddToOnIdle(&gService);
			return true;
		}
		bool PreAppShutdown() override { gStopping = true; return true; }
		bool PostAppShutdown() override { return true; }
		bool PostSystemServiceShutdown() override { return true; }
		bool AbortiveQuit() override { gStopping = true; return true; }
		bool OnInstall() override { return true; }

		// ---- cIGZMessageTarget2 ----
		bool DoMessage(cIGZMessage2* msg) override
		{
			if (msg && msg->GetType() == kMouseMessage) OnMouse(msg);
			return true;
		}

	private:
		uint32_t refs;
		cIGZCOM* com;
		cIGZFrameWork* fw;
	};

	constinit TimeControlDirector gDirector;
}

extern "C" __declspec(dllexport) cIGZCOMDirector* GZDllGetGZCOMDirector()
{
	return static_cast<cIGZCOMDirector*>(&gDirector);
}
