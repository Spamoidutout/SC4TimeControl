// TimeOfDayMenu.cpp - SC4 Time Of Day Menu.
//
// Right click on the "God mode" button: a small Maxis-style menu opens with the
// 24 hours in two columns. A left click on an hour issues the game's own cheat,
// exactly as Ctrl-X "whattimeizit <hour>" does.
//
// No heap, no CRT, no STL. Rules: subscribe once in PostAppInit; the mouse
// message only reads and notes; windows are opened / closed and the cheat is
// issued from the OnIdle of a system service.

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
#include "cISC4App.h"

namespace
{
	// ----- our ids (the .dat ones must match tools/build_dat.py) -----
	constexpr uint32_t kUIGroup       = 0xB99FCED4;   // UI script: {type 0, kUIGroup, kMenu}
	constexpr uint32_t kMenu          = 0x00000001;
	constexpr uint32_t kMenuRoot      = 0x6D366D2D;
	constexpr uint32_t kTitle         = 0x10;
	constexpr uint32_t kHourTextFirst = 0x100;        // + hour
	constexpr uint32_t kHourHitFirst  = 0x200;        // + hour
	constexpr uint32_t kDirectorID    = 0xA770EEFE;
	constexpr uint32_t kServiceID     = 0x8DDD4E83;

	const char kTitleText[] = "Time of Day";
	const char kCheat[]     = "whattimeizit";

	// ----- game ids -----
	constexpr uint32_t kGodModeButton            = 0x2988BC85;
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
	constexpr uint32_t kTextColor                = 0xFF3F4967;   // dark blue of the interface texts

	// The SDK headers for these pull in the STL: declared by hand.
	struct UIResKey { uint32_t type, group, instance; };
	class cIGZUIScriptServiceMin : public cIGZUnknown
	{
	public:
		virtual bool CreateWindowFromScript(UIResKey const& key, cIGZWin* parent, uint32_t rootID, uint32_t riid, void** ppvObj) = 0; // slot 3
	};

	struct Point { int32_t x, y; };

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

	// ======================================================================
	// State
	// ======================================================================
	cIGZFrameWork* gFW = nullptr;
	cISC4App* gApp = nullptr;
	bool gStopping = false;

	cIGZWinGen* gGen = nullptr;   // the menu, when open
	cIGZWin* gWin = nullptr;

	// Noted by the mouse message, handled in OnIdle.
	bool gOpen = false, gClose = false;
	int32_t gPicked = -1, gPressed = -1;
	int32_t gBx = 0, gBy = 0, gBw = 0, gBh = 0;   // God mode button, screen rectangle

	constinit FixedString gText;        // captions
	constinit FixedString gCheatLine;   // "whattimeizit 14", kept alive after the send
	uint32_t gCheatID = 0;

	int32_t HourAt(int32_t x, int32_t y)
	{
		for (int32_t h = 0; h < 24; ++h)
			if (Inside(gWin->GetChildWindowFromID(kHourHitFirst + h), x, y)) return h;
		return -1;
	}

	// ======================================================================
	// From DoMessage: read and note only
	// ======================================================================
	void OnMouse(cIGZMessage2* msg)
	{
		uint32_t event = 0;
		Point pt;
		if (gStopping || !gApp || !ReadMouse(msg, event, pt)) return;

		if (event == kRightUp)
		{
			if (gWin) { gClose = true; return; }   // any right click closes the menu

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
		else if (gWin && event == kLeftDown)
		{
			if (!Inside(gWin, pt.x, pt.y)) gClose = true;
			else gPressed = HourAt(pt.x, pt.y);
		}
		else if (gWin && event == kLeftUp)
		{
			const int32_t h = HourAt(pt.x, pt.y);
			if (h >= 0 && h == gPressed) gPicked = h;   // press and release on the same hour
			gPressed = -1;
		}
	}

	// ======================================================================
	// From OnIdle
	// ======================================================================
	void SetText(uint32_t id, const char* text)
	{
		cIGZWin* c = gWin->GetChildWindowFromID(id);
		cIGZWinText* t = nullptr;
		if (!c || !c->QueryInterface(kGZIID_cIGZWinText, reinterpret_cast<void**>(&t)) || !t) return;
		gText.FromChar(text);
		t->SetBackgroundOpaque(false);
		t->SetTextColor(kTextColor);
		t->SetCaption(gText);
		t->Release();
	}

	void OpenMenu()
	{
		cIGZUIScriptServiceMin* ui = nullptr;
		if (!gFW->GetSystemService(kUIScriptServiceID, kGZIID_cIGZUIScriptService, reinterpret_cast<void**>(&ui)) || !ui)
			return;
		cIGZWin* main = gApp->GetMainWindow();
		cIGZWin* parent = main ? main->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
		if (!parent) parent = main;
		UIResKey key = { 0, kUIGroup, kMenu };
		cIGZWinGen* g = nullptr;
		const bool ok = parent && ui->CreateWindowFromScript(key, parent, kMenuRoot, kGZIID_cIGZWinGen, reinterpret_cast<void**>(&g)) && g;
		ui->Release();
		if (!ok) return;   // the .dat is not in Plugins

		gGen = g;
		gWin = g->AsIGZWin();
		cIGZWin* p = gWin->GetParentWin();
		if (p != parent)
		{
			if (p) p->ChildRemove(gWin);
			parent->ChildAdd(gWin);
		}
		if (gWin->GetFlag(cIGZWin::WinFlag_AcceptFocus)) gWin->SetFlag(cIGZWin::WinFlag_AcceptFocus, false);

		SetText(kTitle, kTitleText);
		char label[6] = { '0', '0', ':', '0', '0', 0 };
		for (int32_t h = 0; h < 24; ++h)
		{
			label[0] = static_cast<char>('0' + h / 10);
			label[1] = static_cast<char>('0' + h % 10);
			SetText(kHourTextFirst + h, label);
		}

		// On the right of the button, centred on it vertically, kept on screen.
		int32_t px, py;
		AbsolutePos(parent, px, py);
		const int32_t w = gWin->GetW(), h = gWin->GetH();
		int32_t x = gBx + gBw + 6 - px;
		int32_t y = gBy + gBh / 2 - h / 2 - py;
		if (x > parent->GetW() - w) x = gBx - w - 6 - px;
		if (y > parent->GetH() - h) y = parent->GetH() - h;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		gWin->GZWinMoveTo(x, y);
		gWin->ShowWindow();
		gWin->PullToFront();
		gPressed = gPicked = -1;
	}

	void CloseMenu()
	{
		if (!gWin) return;
		gWin->HideWindow();
		cIGZWin* p = gWin->GetParentWin();
		if (p) p->ChildRemove(gWin);
		gGen->Release();
		gGen = nullptr;
		gWin = nullptr;
		gPressed = gPicked = -1;
	}

	// Issues "whattimeizit <hour>", as the Ctrl-X box does.
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

	void IdleWork()
	{
		if (gStopping) return;
		const bool city = gApp && gApp->GetCity() != nullptr;

		if (gWin && (!city || gClose || gPicked >= 0))
		{
			const int32_t h = gPicked;
			CloseMenu();
			if (city && h >= 0) SetHour(h);
		}
		else if (!gWin && gOpen && city)
		{
			OpenMenu();
		}
		gOpen = gClose = false;
	}

	// ======================================================================
	// COM objects (constant-initialised statics: no global constructors)
	// ======================================================================
	class TimeOfDayService final : public cIGZSystemService
	{
	public:
		constexpr TimeOfDayService() : refs(0), id(kServiceID), running(false) {}

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

	constinit TimeOfDayService gService;

	class TimeOfDayDirector final : public cIGZCOMDirector, public cIGZFrameWorkHooks, public cIGZMessageTarget2
	{
	public:
		constexpr TimeOfDayDirector() : refs(0), com(nullptr), fw(nullptr) {}

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

	constinit TimeOfDayDirector gDirector;
}

extern "C" __declspec(dllexport) cIGZCOMDirector* GZDllGetGZCOMDirector()
{
	return static_cast<cIGZCOMDirector*>(&gDirector);
}
