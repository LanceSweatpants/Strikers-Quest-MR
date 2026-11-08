#include "Game/SH/SHPause.h"
#include "Game/OverlayManager.h"
#include "Game/SH/SHLessonSelect.h"

#include "Game/FE/FEAudio.h"
#include "Game/FE/Overlay/OverlayHandlerSummary.h"
#include "Game/FE/feHelpFuncs.h"
#include "Game/FE/feFinder.h"
#include "Game/FE/feManager.h"
#include "Game/FE/feSceneManager.h"
#include "Game/FE/fePopupMenu.h"
#include "Game/FE/tlTextInstance.h"
#include "Game/GameInfo.h"
#include "Game/Game.h"
#include "NL/glx/glxSwap.h"
#include "NL/nlLocalization.h"
#include "NL/nlPrint.h"
#include "NL/nlTask.h"
#include "Game/Team.h"     // strikers-quest r157: the score for the headset's pause panel
#include "port/xr_quest.h" // strikers-quest r157: PortPauseMenu* (the headset's pause panel drives this menu)
#include "port/xr_net.h"   // strikers-quest r157: PortNetLocalPad (the pad that quits)
#include "Game/DB/UserOptions.h"

extern FEInput* g_pFEInput;

// strikers-quest r157 (menu overhaul; Lance: "i want those menus to be easy to navigate"): the headset's pause panel is
// the one cursor now (r42-r156: this menu on the left stick and the port's own panel on the right stick, at once). This
// menu stays underneath: the panel reads its state and picks its items through its own handlers - the same as A on them -
// so the quit / forfeit / Strikers 101 wording and flow stay the game's.
static PauseMenuScene* s_portPause = NULL;
static int s_portPauseAsk = -1;

extern "C" int PortPauseMenuState(void)
{
    PauseMenuScene* p = s_portPause;
    if (p == NULL)
        return 0;
    if (OverlayManager::Instance()->GetCurrentScene() != (BaseSceneHandler*)p)
        return -1; // a screen of its own over it
    if (p->mIsInTransition || p->mQuitDelay > 0.0f || FrontEnd::m_bGameOver)
        return -1;
    return p->mContext == PauseMenuScene::SC_101_PAUSE ? 2 : 1;
}

extern "C" void PortPauseMenuSelect(int what)
{
    s_portPauseAsk = what;
}

// strikers-quest r157: the game's own AUDIO / VISUAL OPTIONS values as the pause panel's rows - read, and written exactly
// as those screens write them (feOptionsSubMenus.cpp OptionsAudioMenuV2 / OptionsVisualMenuV2::Save: into the user's
// options, the volumes applied at once with ApplySettings(false, false); the camera ones read by the next gameplay
// camera, as from the game's own screen). Volumes and the zoom 0..10. The sound mode isn't changed in a pause: the
// game's own pause screen hides and disables it (it reloads the music) - PORT_OPT_MODE is read only.
extern "C" int PortPauseOption(int which)
{
    const UserInfo& u = nlSingleton<GameInfoManager>::Instance()->mUserInfo;
    switch (which)
    {
    case PORT_OPT_MUSIC: return u.mAudioOptions.MusicVolume;
    case PORT_OPT_SFX: return u.mAudioOptions.SFXVolume;
    case PORT_OPT_VOICE: return u.mAudioOptions.VoiceVolume;
    case PORT_OPT_MODE: return (int)u.mAudioOptions.Mode;
    case PORT_OPT_AUTOZOOM: return u.mVisualOptions.mIsAutoZoomCamera ? 1 : 0;
    case PORT_OPT_ZOOM: return (int)(10.0f * u.mVisualOptions.mCameraZoomLevel + 0.5f);
    case PORT_OPT_WIDESCREEN: return u.mVisualOptions.mIsWidescreen ? 1 : 0;
    default: return 0;
    }
}

extern "C" void PortPauseOptionSet(int which, int value)
{
    UserInfo& u = nlSingleton<GameInfoManager>::Instance()->mUserInfo;
    const int v10 = value < 0 ? 0 : (value > 10 ? 10 : value);
    switch (which)
    {
    case PORT_OPT_MUSIC: u.mAudioOptions.MusicVolume = v10; break;
    case PORT_OPT_SFX: u.mAudioOptions.SFXVolume = v10; break;
    case PORT_OPT_VOICE: u.mAudioOptions.VoiceVolume = v10; break;
    case PORT_OPT_AUTOZOOM: u.mVisualOptions.mIsAutoZoomCamera = value != 0; return;
    case PORT_OPT_ZOOM: u.mVisualOptions.mCameraZoomLevel = (float)v10 / 10.0f; return;
    case PORT_OPT_WIDESCREEN: u.mVisualOptions.mIsWidescreen = value != 0; return;
    default: return; // (PORT_OPT_MODE: not in a pause)
    }
    u.mAudioOptions.ApplySettings(false, false);
}

extern "C" int PortPauseScore(int* home, int* away)
{
    if (g_pTeams[0] == NULL || g_pTeams[1] == NULL)
        return 0;
    *home = g_pTeams[0]->m_nScore;
    *away = g_pTeams[1]->m_nScore;
    return 1;
}
extern nlColour MenuHighliteColour;


static char sPauseOutSlide[] = "out";
eFEINPUT_PAD PauseMenuScene::mControllingInput = FE_ALL_PADS;
float PauseMenuScene::mDelayBeforeUnpause = 0.1f;
u32 PauseMenuScene::mLastTaskManagerState;
s32 PauseMenuScene::mLastSelectedIndex;

namespace DoubleHighlite
{
void OpenItem(TLComponentInstance*);
}

#include "NL/nlBind.h"

typedef Detail::MemFunImpl<void, void (PauseMenuScene::*)()> MemFunImpl_Pause_v_t;
typedef Detail::MemFunImpl<void, void (PauseMenuScene::*)(TLComponentInstance*)> MemFunImpl_Pause_p_t;
typedef BindExp1<void, MemFunImpl_Pause_v_t, PauseMenuScene*> BindExp1_Pause_t;
typedef BindExp2<void, MemFunImpl_Pause_p_t, PauseMenuScene*, Placeholder<0> > BindExp2_Pause_t;

/**
 * Offset/Address/Size: 0x225C | 0x800AF754 | size: 0xDC
 */
PauseMenuScene::PauseMenuScene(PauseMenuScene::ScreenContext context)
    : BaseSceneHandler()
    , mContext(context)
    , mGameIsOver(false)
    , mQuitDelay(0.0f)
    , mQuittingController(FE_ALL_PADS)
    , mMenuItems()
    , mTransitionTo(TT_IN)
    , mIsInTransition(false)
    , mStartAnimAtEnd(false)
    , mButtons()
    , mButtons2()
{
    mDelayBeforeUnpause = 0.1f;
}

/**
 * Offset/Address/Size: 0x21AC | 0x800AF6A4 | size: 0xB0
 */
PauseMenuScene::~PauseMenuScene()
{
    if (s_portPause == this) // strikers-quest r157
    {
        s_portPause = NULL;
        s_portPauseAsk = -1;
    }
}

/**
 * Offset/Address/Size: 0x2158 | 0x800AF650 | size: 0x54
 */
void PauseMenuScene::OnSelectRESUME(TLComponentInstance* instance)
{
    TransitionOut(TT_OUT);
    g_pFEInput->Reset();
    FEAudio::PlayAnimAudioEvent("sfx_screen_back", false);
    FEAudio::PlayAnimAudioEvent("sfx_pause_resume", false);
    mLastSelectedIndex = 0;
}

/**
 * Offset/Address/Size: 0x18CC | 0x800AEDC4 | size: 0x88C
 */
void PauseMenuScene::OnSelectQUIT(TLComponentInstance* instance)
{
    FEPopupMenu* popup;

    if (FrontEnd::m_bGameOver)
    {
        OverlayManager::Instance()->Pop();
        OverlayManager::Instance()->Pop();
        OverlayManager::Instance()->Push(OVERLAY_BRAG, SCREEN_FORWARD, false);
    }
    else
    {
        popup = (FEPopupMenu*)OverlayManager::Instance()->Push(OVERLAY_POPUP, SCREEN_NOTHING, false);
        popup->mControlInput = mQuittingController;

        if (nlSingleton<GameInfoManager>::Instance()->mIsInStrikers101Mode)
        {
            popup->Create(
                POPUP_INGAME_QUIT_STRIKERS_101,
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupYESFORFEIT), this),
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupNOFORFEIT), this));
        }
        else if (nlSingleton<GameInfoManager>::Instance()->mCurrentMode == GameInfoManager::GM_FRIENDLY || g_pGame->m_eGameState == GS_END_GAME)
        {
            popup->Create(
                POPUP_INGAME_QUIT_MATCH,
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupYESFORFEIT), this),
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupNOFORFEIT), this));
        }
        else if (nlSingleton<GameInfoManager>::Instance()->IsInCupMode()
                 || (nlSingleton<GameInfoManager>::Instance()->IsInTournamentMode()
                     && nlSingleton<GameInfoManager>::Instance()->GetPlayingSide((unsigned short)mQuittingController) != -1))
        {
            popup->Create(
                POPUP_INGAME_FORFEIT_MATCH,
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupYESFORFEIT), this),
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupNOFORFEIT), this));
        }
        else
        {
            popup->Create(
                POPUP_NO_FORFEIT,
                Bind<void>(MemFun<PauseMenuScene, void>(&PauseMenuScene::OnSelectPopupNOFORFEIT), this));
        }
    }
}

/**
 * Offset/Address/Size: 0x1890 | 0x800AED88 | size: 0x3C
 */
void PauseMenuScene::OnSelectCHOOSESIDES(TLComponentInstance* instance)
{
    OverlayManager::Instance()->Push(IGSCENE_CHOOSE_SIDES, SCREEN_FORWARD, true);
}

/**
 * Offset/Address/Size: 0x1854 | 0x800AED4C | size: 0x3C
 */
void PauseMenuScene::OnSelectAUDIOOPTIONS(TLComponentInstance* instance)
{
    OverlayManager::Instance()->Push(IGSCENE_PAUSE_AUDIO, SCREEN_FORWARD, true);
}

/**
 * Offset/Address/Size: 0x1818 | 0x800AED10 | size: 0x3C
 */
void PauseMenuScene::OnSelectVISUALOPTIONS(TLComponentInstance* instance)
{
    OverlayManager::Instance()->Push(IGSCENE_PAUSE_VISUAL, SCREEN_FORWARD, true);
}

/**
 * Offset/Address/Size: 0x17CC | 0x800AECC4 | size: 0x4C
 */
void PauseMenuScene::OnSelectSTATISTICS(TLComponentInstance* instance)
{
    SummaryOverlay* scene = (SummaryOverlay*)OverlayManager::Instance()->Push(OVERLAY_SUMMARY_PAUSE, SCREEN_FORWARD, true);
    scene->m_controllingInput = mControllingInput;
    scene->mButtonState = ButtonComponent::BS_B_ONLY;
}

void PauseMenuScene::OnSelectBRAGGING(TLComponentInstance* instance)
{
}

/**
 * Offset/Address/Size: 0x17C8 | 0x800AECC0 | size: 0x4
 */
void PauseMenuScene::OnSelectPopupNOFORFEIT()
{
}

/**
 * Offset/Address/Size: 0x1684 | 0x800AEB7C | size: 0x144
 */
void PauseMenuScene::OnSelectPopupYESFORFEIT()
{
    GameInfoManager* gameInfoManager;
    s32 quittingSide;

    gameInfoManager = nlSingleton<GameInfoManager>::s_pInstance;

    if (gameInfoManager->mIsInStrikers101Mode)
    {
        mQuitDelay = 1.0f;
        return;
    }

    if (g_pGame->m_eGameState != GS_END_GAME)
    {
        gameInfoManager = nlSingleton<GameInfoManager>::s_pInstance;
        quittingSide = -1;

        if (gameInfoManager->IsInCupMode())
        {
            eTeamID userTeam = gameInfoManager->GetUserSelectedCupTeam();
            if (userTeam == gameInfoManager->GetTeam(0))
            {
                quittingSide = 0;
            }
            else if (userTeam == gameInfoManager->GetTeam(1))
            {
                quittingSide = 1;
            }
        }
        else if (gameInfoManager->IsInTournamentMode())
        {
            quittingSide = gameInfoManager->GetPlayingSide(mQuittingController);
        }

        if (gameInfoManager->IsInCupOrTournamentMode())
        {
            if (quittingSide == 0)
            {
                nlSingleton<StatsTracker>::Instance()->TrackWinner(0);
                gameInfoManager->SetResultsOfLastUserGame((eUserGameResult)0xD);
            }
            else if (quittingSide == 1)
            {
                nlSingleton<StatsTracker>::Instance()->TrackWinner(1);
                gameInfoManager->SetResultsOfLastUserGame((eUserGameResult)0xE);
            }
        }
    }

    mQuitDelay = 1.0f;
}

/**
 * Offset/Address/Size: 0x1640 | 0x800AEB38 | size: 0x44
 */
void PauseMenuScene::OnSelectLESSONS(TLComponentInstance* instance)
{
    LessonSelectScene* scene = (LessonSelectScene*)OverlayManager::Instance()->Push(IGSCENE_LESSON_SELECT, SCREEN_FORWARD, true);
    scene->mStartAnimAtEnd = true;
}

void PauseMenuScene::StartDelayedQuit()
{
    mQuitDelay = 1.0f;
}

/**
 * Offset/Address/Size: 0x84C | 0x800ADD44 | size: 0xDF4
 */
void PauseMenuScene::SceneCreated()
{
    extern bool g_e3_Build;

    typedef Detail::MemFunImpl<void, void (PauseMenuScene::*)(TLComponentInstance*)> PauseMemFun;
    typedef BindExp2<void, PauseMemFun, PauseMenuScene*, Placeholder<0> > PauseBind;
    typedef MenuItem<TLComponentInstance>::Callback MenuCallback;

    FEAudio::EnableSounds(false);
    s_portPause = this; // strikers-quest r157: the headset's pause panel drives this menu
    s_portPauseAsk = -1;

    switch (mContext)
    {
    case SC_REGULAR_PAUSE:
    {
        FEPresentation* presentation = m_pFEScene->m_pFEPackage->GetPresentation();

        static void (PauseMenuScene::* PauseMenuCBs[6])(TLComponentInstance*) = {
            &PauseMenuScene::OnSelectRESUME,
            &PauseMenuScene::OnSelectCHOOSESIDES,
            &PauseMenuScene::OnSelectAUDIOOPTIONS,
            &PauseMenuScene::OnSelectVISUALOPTIONS,
            &PauseMenuScene::OnSelectSTATISTICS,
            &PauseMenuScene::OnSelectQUIT,
        };

        static char* MENU_NAMES[6]
            = { "MENU ITEM1", "MENU ITEM2", "MENU ITEM3", "MENU ITEM6", "MENU ITEM4", "MENU ITEM5" };

        static const bool E3_BUILD_IS_DISABLED_OPTIONS[6] = { false, false, true, true, false, false };

        int i;
        for (i = 0; i < 6; i++)
        {
            TLInstance* instance = FEFinder<TLInstance, 4>::Find<TLSlide>(
                presentation->m_currentSlide,
                InlineHasher(nlStringLowerHash("Layer")),
                InlineHasher(nlStringLowerHash(MENU_NAMES[i])));
            TLComponentInstance* compinstance = (TLComponentInstance*)instance;

            MenuItem<TLComponentInstance>* menuItem = mMenuItems.AddItem(compinstance);

            void (PauseMenuScene::*openCB)(TLComponentInstance*) = &PauseMenuScene::OpenItem;
            {
                MenuCallback openFunc(Bind<void>(MemFun<PauseMenuScene, void, TLComponentInstance*>(openCB), this, placeholder0));
                menuItem->SetCallback(ON_HIGHLIGHT, openFunc);
            }

            {
                MenuCallback closeFunc(Bind<void>(MemFun<PauseMenuScene, void, TLComponentInstance*>(&PauseMenuScene::CloseItem), this, placeholder0));
                menuItem->SetCallback(ON_UNHIGHLIGHT, closeFunc);
            }

            if (PauseMenuCBs[i])
            {
                MenuCallback applyFunc(Bind<void>(MemFun<PauseMenuScene, void, TLComponentInstance*>(PauseMenuCBs[i]), this, placeholder0));
                menuItem->SetCallback(ON_APPLY, applyFunc);
            }

            (void)FindComponent(compinstance->GetActiveSlide(), "highlite");

            if (i == mLastSelectedIndex)
            {
                menuItem->RunCallback(ON_HIGHLIGHT);
            }
            else
            {
                menuItem->RunCallback(ON_UNHIGHLIGHT);

                TLSlide* slide = compinstance->GetActiveSlide();
                compinstance->Update(1.0f + (slide->m_start + slide->m_duration));
            }

            if (i == 5)
            {
                SetQuitTextForJapanese(compinstance);
            }

            if (g_e3_Build)
            {
                menuItem->SetDisabledFlag(E3_BUILD_IS_DISABLED_OPTIONS[i]);
            }
        }

        mMenuItems.SetFlag(1);
        mMenuItems.SetActiveItemIndex(mLastSelectedIndex);
        mMenuItems.RunCallbackOnCurrent(ON_HIGHLIGHT);
        break;
    }
    case SC_101_PAUSE:
    {
        FEPresentation* presentation = m_pFEScene->m_pFEPackage->GetPresentation();

        static void (PauseMenuScene::* PauseMenuCBs[3])(TLComponentInstance*) = {
            &PauseMenuScene::OnSelectLESSONS,
            &PauseMenuScene::OnSelectRESUME,
            &PauseMenuScene::OnSelectQUIT,
        };

        static char* MENU_NAMES[3] = { "MENU ITEM1", "MENU ITEM2", "MENU ITEM3" };

        int i;
        for (i = 0; i < 3; i++)
        {
            TLInstance* instance = FEFinder<TLInstance, 4>::Find<TLSlide>(
                presentation->m_currentSlide,
                InlineHasher(nlStringLowerHash("Layer")),
                InlineHasher(nlStringLowerHash(MENU_NAMES[i])));
            TLComponentInstance* compinstance = (TLComponentInstance*)instance;

            MenuItem<TLComponentInstance>* menuItem = mMenuItems.AddItem(compinstance);

            {
                MenuCallback openFunc(DoubleHighlite::OpenItem);
                menuItem->SetCallback(ON_HIGHLIGHT, openFunc);
            }

            {
                MenuCallback closeFunc(DoubleHighlite::CloseItem);
                menuItem->SetCallback(ON_UNHIGHLIGHT, closeFunc);
            }

            if (PauseMenuCBs[i])
            {
                MenuCallback applyFunc(Bind<void>(MemFun<PauseMenuScene, void, TLComponentInstance*>(PauseMenuCBs[i]), this, placeholder0));
                menuItem->SetCallback(ON_APPLY, applyFunc);
            }

            (void)FindComponent(compinstance->GetActiveSlide(), "highlite");

            if (i == mLastSelectedIndex)
            {
                menuItem->RunCallback(ON_HIGHLIGHT);
            }
            else
            {
                menuItem->RunCallback(ON_UNHIGHLIGHT);

                TLSlide* slide = compinstance->GetActiveSlide();
                compinstance->Update(1.0f + (slide->m_start + slide->m_duration));
            }
        }

        mMenuItems.SetFlag(1);
        mMenuItems.SetActiveItemIndex(mLastSelectedIndex);
        mMenuItems.RunCallbackOnCurrent(ON_HIGHLIGHT);
        break;
    }
    default:
        break;
    }

    TLComponentInstance* buttonComponent = FEFinder<TLComponentInstance, 4>::Find<TLSlide>(
        m_pFEPresentation->m_currentSlide,
        InlineHasher(nlStringLowerHash("Layer")),
        InlineHasher(nlStringLowerHash("buttons")));
    mButtons.mButtonInstance = buttonComponent;
    mButtons.SetState(ButtonComponent::BS_A_AND_B);

    buttonComponent = FEFinder<TLComponentInstance, 4>::Find<FEPresentation>(
        m_pFEPresentation,
        InlineHasher(nlStringLowerHash("menu in2")),
        InlineHasher(nlStringLowerHash("Layer")),
        InlineHasher(nlStringLowerHash("buttons")));
    mButtons2.mButtonInstance = buttonComponent;
    mButtons2.SetState(ButtonComponent::BS_A_AND_B);

    EnableAutoPressed();
    FEAudio::EnableSounds(true);
}

/**
 * Offset/Address/Size: 0x268 | 0x800AD760 | size: 0x5E4
 */
void PauseMenuScene::Update(float fDeltaT)
{
    if (mQuitDelay > 0.0f)
    {
        mQuitDelay = mQuitDelay - fDeltaT;
        if (!nlSingleton<OverlayManager>::Instance()->IsOnStack(OVERLAY_POPUP))
        {
            glxSwapSetBlack(true);
        }
        if (mQuitDelay <= 0.0f)
        {
            mQuitDelay = 0.0f;
            FrontEnd::ReturnToFE();
        }
        return;
    }

    if (mStartAnimAtEnd)
    {
        if (m_pFEPresentation->m_currentSlide != NULL)
        {
            m_pFEPresentation->m_fadeDuration = 999.9f;
            mStartAnimAtEnd = false;
        }
    }

    BaseSceneHandler::Update(fDeltaT);
    mButtons.CentreButtons();
    mButtons2.CentreButtons();

    if (mIsInTransition)
    {
        TLSlide* slide = m_pFEPresentation->m_currentSlide;
        f32 currentTime = slide->m_time;
        f32 endTime = slide->m_start + slide->m_duration;
        if (!(currentTime >= endTime))
            return;

        switch (mTransitionTo)
        {
        case TT_OUT:
            FrontEnd::ExitMenuState();
            break;
        default:
            break;
        }
        mIsInTransition = false;
        mTransitionTo = (TransitionType)0;
        return;
    }

    u8* connState;
    u8 goToChooseSides = 0;
    int i = 0;
    connState = &FrontEnd::m_ctrlConnectedState[0];

    for (; i < 4; i++)
    {
        bool curConnected = g_pFEInput->IsConnected((eFEINPUT_PAD)i);

        if (!g_pFEInput->IsConnected((eFEINPUT_PAD)i))
        {
            if (nlSingleton<GameInfoManager>::Instance()->GetPlayingSide((unsigned short)i) != -1)
            {
                if (!goToChooseSides)
                {
                    OverlayManager* overlayManager = nlSingleton<OverlayManager>::s_pInstance;
                    while ((overlayManager = nlSingleton<OverlayManager>::s_pInstance)->GetCurrentScene() != (BaseSceneHandler*)this)
                    {
                        overlayManager->Pop();
                        nlSingleton<FESceneManager>::Instance()->ForceImmediateStackProcessing();
                    }
                    overlayManager->Push(IGSCENE_CHOOSE_SIDES, SCREEN_FORWARD, true);
                }
                goToChooseSides = 1;
            }
        }

        *connState = curConnected;
        connState++;
    }

    if (goToChooseSides)
        return;

    mDelayBeforeUnpause = mDelayBeforeUnpause - fDeltaT;
    if (mDelayBeforeUnpause > 0.0f)
        return;

    mDelayBeforeUnpause = 0.0f;

    if (m_pFEPresentation->m_currentSlide == NULL)
        return;

    // strikers-quest r157: the headset's pause panel picked an item: its own handler, exactly as A on it (the item lit
    // first, as the stick would). Afterwards the menu opens on RESUME again (Lance: pause opens on RESUME every time).
    if (s_portPauseAsk >= 0)
    {
        const int what = s_portPauseAsk;
        s_portPauseAsk = -1;
        int index = -1;
        int resumeIndex = 0;
        if (mContext == SC_REGULAR_PAUSE)
        {
            index = what <= PORT_PAUSE_QUIT ? what : -1; // (PauseMenuCBs' order)
        }
        else if (mContext == SC_101_PAUSE)
        {
            resumeIndex = 1;
            index = what == PORT_PAUSE_LESSONS ? 0 : (what == PORT_PAUSE_RESUME ? 1 : (what == PORT_PAUSE_QUIT ? 2 : -1));
        }
        if (index >= 0 && index < mMenuItems.GetNumItemsAdded())
        {
            if (index != mMenuItems.GetActiveItemIndex())
                mMenuItems.SetItem(index);
            // the pad "pressing" it: the one this headset plays on (pad 0 offline - the only place the panel drives
            // this menu; online it's off, lockstep). The game takes the quitter from it: a tournament's forfeit is
            // GetPlayingSide(that pad); a cup's is the user's cup team, Strikers 101 and a quick match need none.
            // mControllingInput is FE_ALL_PADS for every pause (feManager.cpp), so it can't say.
            mQuittingController = (eFEINPUT_PAD)PortNetLocalPad();
            if (nlSingleton<GameInfoManager>::Instance()->GetPlayingSide((unsigned short)mQuittingController) == -1)
            {
                for (int pad = 0; pad < 4; pad++) // (that pad on no side: the first pad that is)
                {
                    if (nlSingleton<GameInfoManager>::Instance()->GetPlayingSide((unsigned short)pad) != -1)
                    {
                        mQuittingController = (eFEINPUT_PAD)pad;
                        break;
                    }
                }
            }
            switch (mMenuItems.RunCallbackOnCurrent(ON_APPLY))
            {
            case RES_OK:
                FEAudio::PlayAnimAudioEvent("sfx_accept", false);
                break;
            case RES_ITEM_DISABLED:
                FEAudio::PlayAnimAudioEvent("sfx_deny", false);
                break;
            default:
                break;
            }
            mLastSelectedIndex = resumeIndex;
        }
        return;
    }

    if (g_pFEInput->IsAutoPressed(mControllingInput, 0xd, true, NULL))
    {
        mMenuItems.PreviousItem();
        return;
    }

    if (g_pFEInput->IsAutoPressed(mControllingInput, 0xe, true, NULL))
    {
        mMenuItems.NextItem();
        return;
    }

    if (g_pFEInput->JustPressed(mControllingInput, 0x100, false, &mQuittingController))
    {
        switch (mMenuItems.RunCallbackOnCurrent(ON_APPLY))
        {
        case RES_OK:
            mLastSelectedIndex = mMenuItems.GetActiveItemIndex();
            FEAudio::PlayAnimAudioEvent("sfx_accept", false);
            break;
        case RES_ITEM_DISABLED:
            FEAudio::PlayAnimAudioEvent("sfx_deny", false);
            break;
        default:
            break;
        }
        return;
    }

    if (!g_pFEInput->JustPressed(mControllingInput, 0x200, false, NULL))
    {
        if (!g_pFEInput->JustPressed(mControllingInput, 0x1000, false, NULL))
            return;
        if (FrontEnd::m_bGameOver)
            return;
    }

    if (!FrontEnd::m_bGameOver)
    {
        OnSelectRESUME(NULL);
    }
    else
    {
        FrontEnd::ExitMenuState();
        FEAudio::PlayAnimAudioEvent("sfx_back", false);
    }
}

void PauseMenuScene::SetupForNewGame()
{
    nlTaskManager* taskManager = nlTaskManager::m_pInstance;
    mLastTaskManagerState = taskManager->m_CurrState;
    taskManager->m_Locked = false;
    nlTaskManager::SetNextState(1);
}

/**
 * Offset/Address/Size: 0xD0 | 0x800AD5C8 | size: 0x198
 */
void PauseMenuScene::TransitionOut(PauseMenuScene::TransitionType newtype)
{
    mIsInTransition = true;
    mTransitionTo = newtype;

    if (mTransitionTo == TT_OUT)
    {
        FEPresentation* presentation = m_pFEScene->m_pFEPackage->GetPresentation();
        presentation->SetActiveSlide("menu in2");
        presentation->Update(0.0f);

        int i;
        for (i = 0; i < mMenuItems.GetNumItemsAdded(); i++)
        {

            char menuname[64];
            nlSNPrintf(menuname, sizeof(menuname), "MENU ITEM%d", i + 1);

            TLInstance* instance = FEFinder<TLInstance, 4>::Find<TLSlide>(
                presentation->m_currentSlide,
                InlineHasher(nlStringLowerHash("Layer")),
                InlineHasher(nlStringLowerHash(menuname)));

            TLComponentInstance* compinstance = (TLComponentInstance*)instance;

            if (i == mMenuItems.GetActiveItemIndex())
            {
                compinstance->SetActiveSlide(sPauseOutSlide);
                compinstance->Update(0.0f);

                TLComponentInstance* highlite = (TLComponentInstance*)FindComponent(compinstance->GetActiveSlide(), "highlite");
                highlite->SetActiveSlide(sPauseOutSlide);
                highlite->Update(0.0f);
                highlite->SetAssetColour(MenuHighliteColour);
            }
            else
            {
                compinstance->SetActiveSlide(sPauseOutSlide);
                compinstance->Update(0.0f);

                TLComponentInstance* highlite = (TLComponentInstance*)FindComponent(compinstance->GetActiveSlide(), "highlite");
                highlite->m_bVisible = false;
            }

            if (i + 1 == 5)
            {
                SetQuitTextForJapanese(compinstance);
            }
        }
    }
}

/**
 * Offset/Address/Size: 0x0 | 0x800AD4F8 | size: 0xD0
 */
void PauseMenuScene::OpenItem(TLComponentInstance* instance)
{
    DoubleHighlite::OpenItem(instance);

    if (mMenuItems.GetMenuItem()->IsDisabled())
    {
        TLTextInstance* text = FEFinder<TLTextInstance, 3>::Find(
            instance->GetActiveSlide(),
            InlineHasher(nlStringLowerHash("pauseresume")),
            InlineHasher(0),
            InlineHasher(0),
            InlineHasher(0),
            InlineHasher(0),
            InlineHasher(0));

        text->m_LocStrId = 0x38202C30;
        text->m_OverloadFlags |= 0x8;
    }

    if (mMenuItems.GetActiveItemIndex() == 5)
    {
        SetQuitTextForJapanese(instance);
    }
}

void PauseMenuScene::CloseItem(TLComponentInstance* instance)
{
    DoubleHighlite::CloseItem(instance);

    if (mMenuItems.GetActiveItemIndex() == 5)
    {
        SetQuitTextForJapanese(instance);
    }
}

void PauseMenuScene::SetQuitTextForJapanese(TLComponentInstance* instance)
{
    if (g_pLocalization->m_CurrentLanguage == nlLocalization::LangJapanese
        && nlSingleton<GameInfoManager>::Instance()->IsInCupOrTournamentMode())
    {
        TLTextInstance* text = FEFinder<TLTextInstance, 3>::Find<TLSlide>(
            instance->GetActiveSlide(),
            InlineHasher(nlStringLowerHash("pauseresume")));

        if (text != NULL)
        {
            text->m_LocStrId = 0x2718546B;
            text->m_OverloadFlags |= 0x8;
        }
    }
}
