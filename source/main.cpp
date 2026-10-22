//=============================================================================
// main.cpp - Point d'entree N3DS Web
//=============================================================================
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <exception>
#include <new>
#include <cstdlib>
#include <algorithm>

// Allouer 1 Mo de pile pour le thread principal (evite les debordements de pile avec stb_image / curl / layout)
extern "C" {
	u32 __stacksize__ = 1024 * 1024;
}


#include "network/network.hpp"
#include "settings/settings.hpp"
#include "storage/storage.hpp"
#include "render/text.hpp"
#include "render/renderer.hpp"
#include "browser/browser.hpp"
#include "ui/ui.hpp"
#include "input/input.hpp"
#include "diag/diag.hpp"
#include "updater/updater.hpp"
#include <cstdio>

static void onTerminate()
{
	diagCrash("terminate");
	std::abort();
}

static void onNoMemory()
{
	diagCrash("memoire epuisee");
	std::abort();
}

//=============================================================================
// N3DS Web ne fonctionne que sur New Nintendo 3DS / New 2DS XL.
//
// Le navigateur s'appuie sur la memoire et la puissance de la New : pages de
// plusieurs centaines de kilo-octets, rendu logiciel complet et plusieurs
// onglets. Sur une 3DS classique ces pages epuiseraient la memoire, donc
// plutot que de proposer un mode degrade on refuse de demarrer clairement.
//=============================================================================
enum class ModelCheck
{
	New3DS,
	Old3DS,
	Unknown,
};

//=============================================================================
// Instantane de diagnostic
//
// L'application n'a pas de console de debogage : elle ecrit sur la carte SD de
// quoi verifier a distance ce qu'elle affiche reellement. L'instantane est
// ecrit une fois au demarrage (il existe donc meme si l'application plante
// ensuite) et a nouveau sur SELECT.
//=============================================================================
static void writeDiag(TextEngine& te, Browser& browser)
{
	DiagSnapshot snap;
	snap.magic = 0x5733444E; // 'N3DW'
	snap.tabCount = browser.count();
	snap.pageHeight = browser.currentTab().page.height;
	snap.scrollY = browser.scrollY();

	DiagFontInfo& f = snap.font;
	f.fontReady = te.ready();
	if (f.fontReady)
	{
		f.cellWidth = te.cellWidth();
		f.cellHeight = te.cellHeight();
		f.sheetWidth = te.sheetWidthPx();
		f.sheetHeight = te.sheetHeightPx();
		f.nRows = te.rowsPerSheet();
		f.nLines = te.linesPerSheet();
		f.sheetFmt = te.sheetFormatCode();
		f.charPerSheet = te.charactersPerSheet();
		f.sheetCount = te.sheetCount();
		f.sheetSize = te.sheetSizeBytes();
		f.textScale = te.textScale();
		f.fallback = te.fallbackGlyph();
		te.glyphStats(f.glyphMin, f.glyphMax, f.sheetMax);

		// Caracteres sondes : on veut savoir dans quelle plage d'indices le
		// systeme place les lettres latines. Si 'A' tombe dans les 200
		// premiers indices, la table de correspondance n'est pas celle de la
		// vraie police de la console.
		static const char kProbes[8] = { 'A', 'a', 'M', 'W', '0', ' ', 0xE9, '.' };
		for (int i = 0; i < 8; i++)
		{
			const u32 cp = (u32)(unsigned char)kProbes[i];
			const int gi = te.glyphIndexOfCodePoint(cp);
			f.probeLabel[i][0] = (kProbes[i] >= 32 && kProbes[i] < 127) ?
				kProbes[i] : '?';
			f.probeLabel[i][1] = '\0';
			f.probeIndex[i] = gi;
			f.probeAdvance[i] = (gi >= 0) ? te.glyphAdvance(gi) : 0.0f;
			f.probeCount = i + 1;
		}

		// Raster du glyphe 'A' : permet de voir si les lettres se decodent.
		const int giA = te.glyphIndexOfCodePoint('A');
		if (giA >= 0 && te.cellWidth() > 0 && te.cellHeight() > 0)
		{
			static u8 raster[512 * 512];
			const int w = te.cellWidth();
			const int h = te.cellHeight();
			if (w <= 512 && h <= 512)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++)
						raster[y * w + x] = te.glyphAlphaAt(giA, x, y);
				diagDumpGlyph("sdmc:/N3DSWeb/diag_glyph.bin", raster, w, h);
				diagBootNote("glyph_A_index", giA);
			}
		}
	}

	// Ecran du haut : lecture directe du framebuffer (RGB565). Les dimensions
	// renvoyees par gfxGetFramebuffer sont transposees (largeur = lignes),
	// c'est normal : le framebuffer de la 3DS est range en colonnes.
	u16 topW = 0, topH = 0;
	u16* topFb = (u16*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &topW, &topH);
	snap.topWidth = topW;
	snap.topHeight = topH;

	diagDump(snap, topFb, topW, topH, nullptr, 0, 0);
}

static ModelCheck detectModel()
{
	if (R_FAILED(cfguInit()))
		return ModelCheck::Unknown;

	u8 model = 0;
	Result res = CFGU_GetSystemModel(&model);
	cfguExit();
	if (R_FAILED(res))
		return ModelCheck::Unknown;

	switch (model)
	{
		case CFG_MODEL_N3DS:
		case CFG_MODEL_N3DSXL:
		case CFG_MODEL_N2DSXL:
			return ModelCheck::New3DS;
		default:
			return ModelCheck::Old3DS;
	}
}

const char* modelDisplayName(ModelCheck m)
{
	switch (m)
	{
		case ModelCheck::New3DS: return "New Nintendo 3DS";
		case ModelCheck::Old3DS: return "Nintendo 3DS (non compatible)";
		default:                 return "modele inconnu";
	}
}

static void paintProgressScreen(PageModel& page, TextEngine& text,
	const std::string& message, int progress, uint64_t animation)
{
	page.bmp.clear(0xFF1A1317);
	page.bmp.fillRect(18, 48, 364, 2, 0xFF49343A);
	page.bmp.fillRect(18, 50, 364, 1, 0xFF9A5D42);
	const std::string title = "PIXELNAUTE";
	const int titleX = (NW_PAGE_WIDTH -
		(int)text.measure(title, 1.0f)) / 2;
	text.drawLine(page.bmp, title, titleX, 76, 1.0f,
		0xFFF2E8E8, true, false);
	const int messageX = std::max(8, (NW_PAGE_WIDTH -
		(int)text.measure(message, 0.62f)) / 2);
	text.drawLine(page.bmp, message, messageX, 126, 0.62f,
		0xFFD4C9C9, false, false);
	page.bmp.fillRect(48, 164, 304, 8, 0xFF49343A);
	if (progress >= 0)
	{
		const int width = clamp(progress, 0, 100) * 3;
		if (width > 0) page.bmp.fillRect(48, 164, width, 8, 0xFFF2A25C);
	}
	else
	{
		const int width = 72;
		const int x = 48 + (int)(animation % 5) * 58;
		page.bmp.fillRect(x, 164, width, 8, 0xFFF2A25C);
	}
	page.bmp.fillRect(18, 191, 364, 1, 0xFF49343A);
	page.height = NW_TOP_HEIGHT;
}

static void presentBootProgress(C3D_RenderTarget* target, PageRenderer& renderer,
	PageModel& page, TextEngine& text, const char* message, int progress)
{
	if (!page.bmp.px)
		return;
	paintProgressScreen(page, text, message, progress, timeMs() / 90);
	renderer.forceRedraw();
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(target, C2D_Color32(26, 19, 23, 255));
	C2D_SceneBegin(target);
	renderer.draw(page, 0);
	C3D_FrameEnd(0);
}

// Ecran d'incompatibilite : message centre en couleur 565, puis sortie.
static void showUnsupported(ModelCheck model)
{
	// gfxInitDefault a deja configure la console : on dessine en texte natif.
	PrintConsole console;
	consoleInit(GFX_TOP, &console);
	consoleSelect(&console);
	consoleClear();

	printf("\x1b[31m");
	printf("\n\n");
	printf("        N3DS Web ne peut pas demarrer\n\n");
	printf("\x1b[0m");
	printf("   Modele detecte : %s\n\n", modelDisplayName(model));
	printf("   Ce navigateur est reserve a la New Nintendo 3DS\n");
	printf("   et a la New Nintendo 2DS XL.\n\n");
	printf("   Il ouvre des pages web completes en memoire, ce que\n");
	printf("   la 3DS classique ne peut pas faire.\n\n");
	printf("\x1b[33m   Appuyez sur START pour quitter.\x1b[0m\n");

	gfxFlushBuffers();
	gfxSwapBuffers();
	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		gspWaitForVBlank();
	}
}

//=============================================================================
// Geste tactile sur la page (ecran du haut).
//
// Comportement d'un telephone :
//   - glisser le doigt fait defiler la page en direct ;
//   - relacher le doigt apres un glissement lance une inertie qui decroit ;
//   - un appui court et immobile active le lien ou le champ touche.
//
// C'est le point qui manquait : auparavant tout appui ouvrait immediatement
// le lien survole, ce qui rendait le defilement au doigt impossible.
//=============================================================================
struct PageGesture
{
	bool active = false;
	int startPageY = 0;     // scrollY au moment de l'appui
	int startPx = 0, startPy = 0;
	int moveX = 0, moveY = 0;   // deplacement cumule (valeur absolue max)
	uint64_t startTime = 0;
};

// Tolerance d'un appui : au-dela, c'est un glissement.
static const int kTapMaxMoveY = 12;
static const int kTapMaxMoveX = 10;
static const uint64_t kTapMaxMs = 450;
// Vitesse minimale pour lancer une inertie, en pixels par image.
static const float kInertiaMinSpeed = 1.5f;
// Frottement applique a la vitesse a chaque image.
static const float kInertiaFriction = 0.90f;
// Vitesse minimale avant d'arreter l'inertie.
static const float kInertiaStop = 0.25f;

static bool sIrrstOk = false;

int main(int argc, char** argv)
{
	std::set_terminate(onTerminate);
	std::set_new_handler(onNoMemory);
	diagStage("boot");

	//--- Acceleration CPU New 3DS (804 MHz et coeurs additionnels) ---
	osSetSpeedupEnable(true);

	//--- jalons de demarrage : le fichier boot.txt indique jusqu'ou
	//--- l'application est allee si elle plante.
	diagBoot(0, "demarrage");

	gfxInitDefault();
	diagBoot(1, "gfxInitDefault");

	fsInit();
	diagBoot(2, "fsInit");

	romfsInit();
	diagBoot(3, "romfsInit");

	sIrrstOk = R_SUCCEEDED(irrstInit());
	diagBoot(4, sIrrstOk ? "irrstInit OK" : "irrstInit NON DISPO");

	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
	{
		diagBoot(5, "C3D_Init ECHEC");
		if (sIrrstOk) irrstExit();
		romfsExit(); fsExit(); gfxExit();
		return 1;
	}
	diagBoot(5, "C3D_Init");

	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
	{
		diagBoot(6, "C2D_Init ECHEC");
		C3D_Fini();
		if (sIrrstOk) irrstExit();
		romfsExit(); fsExit(); gfxExit();
		return 1;
	}
	diagBoot(6, "C2D_Init");

	C2D_Prepare();
	diagBoot(7, "C2D_Prepare");

	C3D_RenderTarget* topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	diagBoot(8, "C2D_CreateScreenTarget");
	if (!topTarget || !bottomTarget)
	{
		diagBoot(8, "C2D_CreateScreenTarget ECHEC");
		C2D_Fini(); C3D_Fini();
		if (sIrrstOk) irrstExit();
		romfsExit(); fsExit(); gfxExit();
		return 1;
	}

	//--- verification du modele avant toute allocation lourde ---
	ModelCheck model = detectModel();
	diagBootNote("modele", (long)(int)model);
	if (model != ModelCheck::New3DS)
	{
		diagBoot(9, "modele non compatible - arret");
		showUnsupported(model);
		C2D_Fini();
		C3D_Fini();
		if (sIrrstOk) irrstExit();
		romfsExit();
		fsExit();
		gfxExit();
		return 0;
	}
	diagBoot(7, "modele compatible");

	TextEngine& te = TextEngine::instance();
	const bool fontOk = te.init();
	diagBoot(10, fontOk ? "police OK" : "police INDISPONIBLE");
	diagBootNote("font_cell_w", te.ready() ? te.cellWidth() : -1);
	diagBootNote("font_cell_h", te.ready() ? te.cellHeight() : -1);
	diagBootNote("font_sheet_fmt", te.ready() ? te.sheetFormatCode() : -1);
	diagBootNote("font_sheets", te.ready() ? te.sheetCount() : -1);

	PageRenderer renderer;
	const bool rendererOk = renderer.init();
	diagBoot(13, rendererOk ? "PageRenderer" : "PageRenderer ECHEC");
	PageModel loadingPage;
	loadingPage.bmp.create(NW_PAGE_WIDTH, NW_TOP_HEIGHT);
	loadingPage.height = NW_TOP_HEIGHT;
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Preparation du stockage", 10);

	storageInit();
	diagBoot(8, "storageInit");

	WebSettings settings;
	settingsLoad(settings);
	diagBoot(9, "settingsLoad");
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Chargement des reglages", 28);

	NetworkManager net;
	net.begin();
	diagBoot(11, "NetworkManager");
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Initialisation du reseau", 48);
	ReleaseUpdater updater;
	u64 runningTitleId = 0;
	const bool ciaTitle = R_SUCCEEDED(APT_GetProgramID(&runningTitleId)) &&
		runningTitleId == 0x000400000FF6A600ULL;
	diagBootNote("running_title_id_low", (long)(runningTitleId & 0xFFFFFFFFULL));
	diagBootNote("cia_updater_mode", ciaTitle ? 1 : 0);
	updater.configure(argc > 0 && argv ? argv[0] : "", ciaTitle);

	Browser browser(net, settings);
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Chargement de l'historique", 65);
	browser.history().load();
	browser.bookmarks().load();
	diagBoot(12, "Browser + historique/favoris");

	// Les vignettes des pages sont dessinees par le moteur d'affichage.
	renderer.setImageStore(&browser.images());

	BottomUi ui;
	const bool uiOk = ui.init();
	diagBoot(14, uiOk ? "BottomUi" : "BottomUi ECHEC");
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Preparation de l'accueil", 84);

	browser.goHome();
	diagBoot(15, "goHome (page d'accueil rendue)");
	presentBootProgress(topTarget, renderer, loadingPage, te,
		"Demarrage termine", 100);

	// Instantane de demarrage : metadonnees seulement, sans image (le
	// framebuffer n'est pas encore dessine a ce stade). L'image complete est
	// prise apres la premiere image rendue, plus bas.
	writeDiag(te, browser);
	diagBoot(16, "instantane de demarrage ecrit");
	diagBootMemory("fin_demarrage");
	bool diagRequested = false;

	int lastTab = -1;
	bool lastReady = false;
	int lastHeight = -1;
	const void* lastPx = nullptr;

	// --- etat du geste et de l'inertie ---
	PageGesture gesture;
	float inertiaSpeed = 0.0f;
	int framesRendered = 0;

	// --- curseur de souris virtuel (C-Stick New 3DS) ---
	float mouseX = 200.0f;
	float mouseY = 120.0f;

	while (aptMainLoop())
	{
		if (updater.restartRequired())
		{
			// APT relancera le titre CIA courant apres sa fermeture propre.
			aptSetChainloaderToSelf();
			break;
		}
		InputState in;
		inputPoll(in);

		// Quitter : START + R
		if ((in.down & KEY_START) && (in.held & KEY_R))
			break;

		// SELECT : nouvel instantane de diagnostic (voir writeDiag).
		if (in.down & KEY_SELECT)
			diagRequested = true;

		ui.handleKeys(browser, net, settings, updater, in.down);
		ui.handleTouch(browser, net, settings, updater, in);
		ui.handleAnalog(browser, net, settings, in);

		// Action interne demandee par un lien nw://
		if (browser.internalNavPending())
			ui.openInternal(browser.takeInternalNav());

		//------------------------------------------------------------------
		// Controles navigation : croix, joystick Circle Pad, C-Stick souris
		//------------------------------------------------------------------
		if (ui.mode() == UiMode::Browsing)
		{
			// 1. Defilement continu a la croix directionnelle (D-Pad)
			if (in.held & KEY_DUP)
			{
				browser.scroll(-10);
				inertiaSpeed = 0.0f;
			}
			else if (in.held & KEY_DDOWN)
			{
				browser.scroll(10);
				inertiaSpeed = 0.0f;
			}

			// 2. Defilement analogique fluide au joystick principal (Circle Pad)
			if (in.circleY > 20 || in.circleY < -20)
			{
				int step = (int)(-((float)in.circleY) * 0.075f);
				if (step != 0)
				{
					browser.scroll(step);
					inertiaSpeed = 0.0f;
				}
			}

			// 3. Deplacement de la souris virtuelle avec le petit joystick (C-Stick New 3DS)
			if (in.cstickX > 12 || in.cstickX < -12 || in.cstickY > 12 || in.cstickY < -12)
			{
				const float sens = 0.045f;
				mouseX += (float)in.cstickX * sens;
				mouseY -= (float)in.cstickY * sens; // cstick dy > 0 vers le haut
				if (mouseX < 0.0f) mouseX = 0.0f;
				if (mouseX > 398.0f) mouseX = 398.0f;
				if (mouseY < 0.0f) mouseY = 0.0f;
				if (mouseY > 238.0f) mouseY = 238.0f;
			}

			// 4. Navigation d'element en element (liens, boutons, champs) avec D-PAD Gauche / Droite
			if ((in.down & KEY_DRIGHT) || (in.down & KEY_DLEFT))
			{
				const Browser::Tab& ct = browser.currentTab();
				if (ct.pageReady)
				{
					struct TargetBox { int x, y, w, h; };
					std::vector<TargetBox> targets;
					targets.reserve(ct.page.links.size() + ct.page.submits.size() + ct.page.fields.size());

					for (const auto& lb : ct.page.links)
						if (lb.w > 0 && lb.h > 0) targets.push_back({ lb.x, lb.y, lb.w, lb.h });
					for (const auto& fs : ct.page.submits)
						if (fs.w > 0 && fs.h > 0) targets.push_back({ fs.x, fs.y, fs.w, fs.h });
					for (const auto& f : ct.page.fields)
						if (f.w > 0 && f.h > 0) targets.push_back({ f.x, f.y, f.w, f.h });

					if (!targets.empty())
					{
						std::sort(targets.begin(), targets.end(), [](const TargetBox& a, const TargetBox& b) {
							if (a.y != b.y) return a.y < b.y;
							return a.x < b.x;
						});

						const int sy = browser.scrollY();
						int bestIdx = -1;
						int currentIdx = -1;

						for (size_t i = 0; i < targets.size(); i++)
						{
							const TargetBox& tb = targets[i];
							if ((int)mouseX >= tb.x && (int)mouseX < tb.x + tb.w &&
							    sy + (int)mouseY >= tb.y && sy + (int)mouseY < tb.y + tb.h)
							{
								currentIdx = (int)i;
								break;
							}
						}

						if (in.down & KEY_DRIGHT)
						{
							for (size_t i = (currentIdx >= 0 ? currentIdx + 1 : 0); i < targets.size(); i++)
							{
								const TargetBox& tb = targets[i];
								if (tb.y + tb.h > sy && tb.y < sy + 240)
								{
									bestIdx = (int)i;
									break;
								}
							}
							if (bestIdx < 0)
							{
								for (size_t i = 0; i < targets.size(); i++)
								{
									const TargetBox& tb = targets[i];
									if (tb.y + tb.h > sy && tb.y < sy + 240) { bestIdx = (int)i; break; }
								}
							}
						}
						else if (in.down & KEY_DLEFT)
						{
							int start = currentIdx >= 0 ? currentIdx - 1 : (int)targets.size() - 1;
							for (int i = start; i >= 0; i--)
							{
								const TargetBox& tb = targets[(size_t)i];
								if (tb.y + tb.h > sy && tb.y < sy + 240)
								{
									bestIdx = i;
									break;
								}
							}
							if (bestIdx < 0)
							{
								for (int i = (int)targets.size() - 1; i >= 0; i--)
								{
									const TargetBox& tb = targets[(size_t)i];
									if (tb.y + tb.h > sy && tb.y < sy + 240) { bestIdx = i; break; }
								}
							}
						}

						if (bestIdx >= 0)
						{
							const TargetBox& tb = targets[(size_t)bestIdx];
							mouseX = (float)(tb.x + tb.w / 2);
							mouseY = (float)(tb.y - sy + tb.h / 2);
							if (mouseY < 4.0f) mouseY = 4.0f;
							if (mouseY > 236.0f) mouseY = 236.0f;
						}
					}
				}
			}

			// 5. Clic de la souris sur le lien/champ survole avec le bouton A
			if (in.down & KEY_A)
			{
				browser.tap((int)mouseX, (int)mouseY);
				if (browser.currentTab().focusNode >= 0)
					ui.startFieldEdit(browser);
			}
		}

		//------------------------------------------------------------------
		// Geste tactile sur l'ecran bas (zone de contenu y >= 88, sous les boutons y=61..85) :
		// glisser = defiler la page, toucher bref = activer le lien/champ
		//------------------------------------------------------------------
		if (ui.mode() == UiMode::Browsing)
		{
			if (in.touchDown && in.tp.py >= 88 && in.tp.px < 320)
			{
				gesture.active = true;
				gesture.startPageY = browser.scrollY();
				gesture.startPx = in.tp.px;
				gesture.startPy = in.tp.py;
				gesture.moveX = 0;
				gesture.moveY = 0;
				gesture.startTime = timeMs();
				inertiaSpeed = 0.0f;
			}

			if (in.touch && gesture.active && in.tp.px < 320)
			{
				const int dx = in.tp.px - gesture.startPx;
				const int dy = in.tp.py - gesture.startPy;
				if (dx > gesture.moveX) gesture.moveX = dx;
				if (-dx > gesture.moveX) gesture.moveX = -dx;
				if (dy > gesture.moveY) gesture.moveY = dy;
				if (-dy > gesture.moveY) gesture.moveY = -dy;
				browser.scrollTo(gesture.startPageY -
					(int)((float)dy * browser.pageScale() + (dy >= 0 ? 0.5f : -0.5f)));
			}

			if (in.touchUp && gesture.active)
			{
				const bool tap = (gesture.moveY <= kTapMaxMoveY) &&
					(gesture.moveX <= kTapMaxMoveX) &&
					(timeMs() - gesture.startTime <= kTapMaxMs);
				if (tap)
				{
					float targetX = (float)gesture.startPx * (400.0f / 320.0f);
					float targetY = (float)(gesture.startPy - 88) * (240.0f / 152.0f);
					mouseX = targetX;
					mouseY = targetY;
					browser.tap((int)targetX, (int)targetY);
					if (browser.currentTab().focusNode >= 0)
						ui.startFieldEdit(browser);
				}
				gesture.active = false;

				if (in.velocityY > kInertiaMinSpeed || in.velocityY < -kInertiaMinSpeed)
					inertiaSpeed = in.velocityY;
			}
		}

		// Inertie : la page continue sur sa lancee en ralentissant.
		if (!gesture.active && inertiaSpeed != 0.0f)
		{
			if (!browser.canScrollBy(inertiaSpeed > 0.0f ? 1 : -1))
			{
				inertiaSpeed = 0.0f;
			}
			else
			{
				browser.scroll((int)inertiaSpeed);
				inertiaSpeed *= kInertiaFriction;
				if (inertiaSpeed < kInertiaStop && inertiaSpeed > -kInertiaStop)
					inertiaSpeed = 0.0f;
			}
		}

		browser.pollNetwork();

		//------------------------------------------------------------------
		// AUTOTEST DU DEFILEMENT (temporaire)
		//
		// Verifie que la page peut reellement defiler, sans dependre des
		// entrees : l'application defile elle-meme, puis ecrit l'image de
		// l'ecran. Comparer les deux images produites montre si le defilement
		// a un effet.
		//------------------------------------------------------------------
		//------------------------------------------------------------------
		// Ecran haut : la page
		//------------------------------------------------------------------
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TargetClear(topTarget, C_BLACK);
		C2D_SceneBegin(topTarget);

		{
			Browser::Tab& t = browser.currentTab();
			if (t.pageReady)
			{
				// detection d'un nouveau rendu (page changee / onglet change)
				if (browser.index() != lastTab || lastReady != true ||
					t.page.height != lastHeight || t.page.bmp.px != lastPx)
				{
					renderer.forceRedraw();
					lastTab = browser.index();
					lastReady = true;
					lastHeight = t.page.height;
					lastPx = t.page.bmp.px;
				}
				renderer.draw(t.page, t.scrollY);
			}
			else
			{
				std::string status = browser.progressStatus();
				int progress = -1;
				if (net.busy())
				{
					status = "Telechargement du site...";
					progress = (int)(net.progress() * 100.0f);
				}
				else if (status.empty())
				{
					status = "Preparation de la page...";
				}
				paintProgressScreen(loadingPage, te, status, progress, timeMs() / 90);
				renderer.forceRedraw();
				renderer.draw(loadingPage, 0);
				if (lastReady)
				{
					lastReady = false;
					renderer.forceRedraw();
				}
			}
		}

		// Dessin du curseur de souris virtuel sur l'ecran du haut
		if (ui.mode() == UiMode::Browsing)
		{
			const float mx = mouseX;
			const float my = mouseY;

			enum class CursorStyle { Arrow, Hand, Text };
			CursorStyle curStyle = CursorStyle::Arrow;

			const Browser::Tab& ct = browser.currentTab();
			if (ct.pageReady)
			{
				const int pageY = browser.scrollY() + browser.pageYFromView((int)my);
				const int imx = browser.pageXFromView((int)mx);

				// 1. Survol d'un champ de formulaire
				for (const FormField& f : ct.page.fields)
				{
					if (imx >= f.x && imx < f.x + f.w && pageY >= f.y && pageY < f.y + f.h)
					{
						if (f.kind == FieldText || f.kind == FieldPassword ||
						    f.kind == FieldTextarea || f.kind == FieldSearch ||
						    f.kind == FieldNumber || f.kind == FieldEmail)
						{
							curStyle = CursorStyle::Text;
						}
						else
						{
							curStyle = CursorStyle::Hand;
						}
						break;
					}
				}

				// 2. Survol d'un bouton de formulaire
				if (curStyle == CursorStyle::Arrow)
				{
					for (const FormSubmit& fs : ct.page.submits)
					{
						if (imx >= fs.x && imx < fs.x + fs.w && pageY >= fs.y && pageY < fs.y + fs.h)
						{
							curStyle = CursorStyle::Hand;
							break;
						}
					}
				}

				// 3. Survol d'un lien
				if (curStyle == CursorStyle::Arrow)
				{
					for (const LinkBox& lb : ct.page.links)
					{
						if (imx >= lb.x && imx < lb.x + lb.w && pageY >= lb.y && pageY < lb.y + lb.h)
						{
							curStyle = CursorStyle::Hand;
							break;
						}
					}
				}
			}

			if (curStyle == CursorStyle::Arrow)
			{
				// Curseur fleche normal : contour noir + corps blanc + tip rouge
				C2D_DrawTriangle(mx - 1.0f, my - 1.0f, C2D_Color32(20, 20, 20, 255),
				                 mx - 1.0f, my + 13.0f, C2D_Color32(20, 20, 20, 255),
				                 mx + 10.0f, my + 9.0f, C2D_Color32(20, 20, 20, 255), 0.5f);
				C2D_DrawTriangle(mx, my, C2D_Color32(255, 255, 255, 255),
				                 mx, my + 11.0f, C2D_Color32(255, 255, 255, 255),
				                 mx + 8.0f, my + 8.0f, C2D_Color32(255, 255, 255, 255), 0.6f);
				C2D_DrawRectSolid(mx, my, 0.7f, 2.0f, 2.0f, C2D_Color32(255, 40, 40, 255));
			}
			else if (curStyle == CursorStyle::Hand)
			{
				// Curseur main (doigt qui pointe) sur un lien ou un bouton :
				//   paume = rectangle blanc, doigt index = petit rectangle fin au-dessus
				const u32 cOut = C2D_Color32(20, 20, 20, 255);
				const u32 cHand = C2D_Color32(250, 230, 60, 255); // jaune vif
				// Doigt (index) : barre fine verticale
				C2D_DrawRectSolid(mx + 2.0f - 1.0f, my - 7.0f - 1.0f, 0.5f, 6.0f, 8.0f, cOut);
				C2D_DrawRectSolid(mx + 2.0f, my - 7.0f, 0.6f, 4.0f, 6.0f, cHand);
				// Paume : rectangle arrondi (4 rects pour simuler)
				C2D_DrawRectSolid(mx - 1.0f, my - 2.0f, 0.5f, 12.0f, 10.0f, cOut);
				C2D_DrawRectSolid(mx, my - 1.0f, 0.6f, 10.0f, 8.0f, cHand);
				// Tip / pointe visible
				C2D_DrawRectSolid(mx + 2.0f, my - 8.0f, 0.7f, 2.0f, 2.0f, C2D_Color32(255, 100, 0, 255));
			}
			else if (curStyle == CursorStyle::Text)
			{
				// Curseur I-beam (zone de texte) : barre en I avec contour pour excellente visibilite
				const u32 cOut = C2D_Color32(20, 20, 20, 255);
				const u32 cInner = C2D_Color32(255, 255, 255, 255);
				const u32 cAccent = C2D_Color32(242, 162, 92, 255); // orange sable

				// Contour noir
				C2D_DrawRectSolid(mx - 3.0f, my - 7.0f, 0.5f, 7.0f, 3.0f, cOut);
				C2D_DrawRectSolid(mx - 3.0f, my + 5.0f, 0.5f, 7.0f, 3.0f, cOut);
				C2D_DrawRectSolid(mx - 1.0f, my - 6.0f, 0.5f, 3.0f, 13.0f, cOut);

				// Corps blanc
				C2D_DrawRectSolid(mx - 2.0f, my - 6.0f, 0.6f, 5.0f, 1.0f, cInner);
				C2D_DrawRectSolid(mx - 2.0f, my + 6.0f, 0.6f, 5.0f, 1.0f, cInner);
				C2D_DrawRectSolid(mx, my - 5.0f, 0.6f, 1.0f, 11.0f, cInner);

				// Repere central
				C2D_DrawRectSolid(mx - 1.0f, my, 0.7f, 3.0f, 1.0f, cAccent);
			}
		}


		//------------------------------------------------------------------
		// Ecran bas : interface
		//------------------------------------------------------------------
		C2D_TargetClear(bottomTarget, C_BLACK);
		C2D_SceneBegin(bottomTarget);
		ui.frame(browser, net, settings, updater);

		C3D_FrameEnd(0);
		if (browser.tickPageResponse())
			renderer.forceRedraw();
		if (browser.tickProgressiveLayout())
			renderer.forceRedraw();
		if (browser.tickStyles())
			renderer.forceRedraw();
		if (browser.tickScripts())
			renderer.forceRedraw();
		if (!updater.active() && browser.tickImages())
			renderer.forceRedraw();

		// Instantane : une fois apres la premiere image (le framebuffer n'est
		// rempli qu'a ce moment-la), puis a la demande sur SELECT.
		framesRendered++;
		if (diagRequested)
		{
			diagRequested = false;
			writeDiag(te, browser);
		}
		if (framesRendered == 200)
			diagBootNote("frames_rendues", framesRendered);
	}

	ui.shutdown();
	renderer.shutdown();
	updater.shutdown();
	net.end();
	if (sIrrstOk) irrstExit();
	romfsExit();
	fsExit();
	gfxExit();
	return 0;
}
