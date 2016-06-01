/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "threads/SystemClock.h"
#include "system.h"
#include "GUIWindowFullScreen.h"
#include "Application.h"
#include "ApplicationMessenger.h"
#include "Util.h"
#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif
#include "GUIInfoManager.h"
#include "guilib/GUIProgressControl.h"
#include "guilib/GUIAudioManager.h"
#include "guilib/GUILabelControl.h"
#include "video/dialogs/GUIDialogVideoOSD.h"
#include "guilib/GUIFontManager.h"
#include "guilib/GUITextLayout.h"
#include "guilib/GUIWindowManager.h"
#include "video/dialogs/GUIDialogFullScreenInfo.h"
#include "video/dialogs/GUIDialogAudioSubtitleSettings.h"
#include "dialogs/GUIDialogNumeric.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "guilib/GUISliderControl.h"
#include "settings/Settings.h"
#include "guilib/GUISelectButtonControl.h"
#include "FileItem.h"
#include "video/VideoReferenceClock.h"
#include "settings/AdvancedSettings.h"
#include "utils/CPUInfo.h"
#include "settings/GUISettings.h"
#include "guilib/LocalizeStrings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "XBDateTime.h"
#include "input/ButtonTranslator.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "windowing/WindowingFactory.h"
#include "cores/IPlayer.h"

#include <stdio.h>
#include <algorithm>
#if defined(TARGET_DARWIN)
#include "linux/LinuxResourceCounter.h"
#endif

/* PLEX */
#include "PlexApplication.h"
#include "Client/PlexServerManager.h"
#include "guilib/GUIImage.h"

#define CONTROL_OVERLAY_START 99000
/* END PLEX */

using namespace PVR;

#define BLUE_BAR                          0
#define LABEL_ROW1                       10
#define LABEL_ROW2                       11
#define LABEL_ROW3                       12
#define CONTROL_GROUP_CHOOSER            503

//Displays current position, visible after seek or when forced
//Alt, use conditional visibility Player.DisplayAfterSeek
#define LABEL_CURRENT_TIME               22

//Displays when video is rebuffering
//Alt, use conditional visibility Player.IsCaching
#define LABEL_BUFFERING                  24

//Progressbar used for buffering status and after seeking
#define CONTROL_PROGRESS                 23

#if defined(TARGET_DARWIN)
static CLinuxResourceCounter m_resourceCounter;
#endif

CGUIWindowFullScreen::CGUIWindowFullScreen(void)
    : CGUIWindow(WINDOW_FULLSCREEN_VIDEO, "VideoFullScreen.xml")
{
  m_timeCodeStamp[0] = 0;
  m_timeCodePosition = 0;
  m_timeCodeShow = false;
  m_timeCodeTimeout = 0;
  m_bShowViewModeInfo = false;
  m_dwShowViewModeTimeout = 0;
  m_bShowCurrentTime = false;
  m_bGroupSelectShow = false;
  m_sliderAction = 0;
  m_loadType = KEEP_IN_MEMORY;
  // audio
  //  - language
  //  - volume
  //  - stream

  // video
  //  - Create Bookmark (294)
  //  - Cycle bookmarks (295)
  //  - Clear bookmarks (296)
  //  - jump to specific time
  //  - slider
  //  - av delay

  // subtitles
  //  - delay
  //  - language
}

CGUIWindowFullScreen::~CGUIWindowFullScreen(void)
{}

bool CGUIWindowFullScreen::OnAction(const CAction &action)
{
  if (g_application.m_pPlayer != NULL && g_application.m_pPlayer->OnAction(action))
    return true;

  if (m_timeCodePosition > 0 && action.GetButtonCode())
  { // check whether we have a mapping in our virtual videotimeseek "window" and have a select action
    CKey key(action.GetButtonCode());
    CAction timeSeek = CButtonTranslator::GetInstance().GetAction(WINDOW_VIDEO_TIME_SEEK, key, false);
    if (timeSeek.GetID() == ACTION_SELECT_ITEM)
    {
      SeekToTimeCodeStamp(SEEK_ABSOLUTE);
      return true;
    }
  }

  const unsigned int MsgTime = 300;
  const unsigned int DisplTime = 2000;

  switch (action.GetID())
  {
  case ACTION_SHOW_OSD:
    ToggleOSD();
    return true;

  case ACTION_TRIGGER_OSD:
    TriggerOSD();
    return true;

  case ACTION_SHOW_GUI:
    {
      // switch back to the menu
      g_windowManager.PreviousWindow();
      return true;
    }
    break;

  case ACTION_PLAYER_PLAY:
  case ACTION_PAUSE:
    if (m_timeCodePosition > 0)
    {
      SeekToTimeCodeStamp(SEEK_ABSOLUTE);
      return true;
    }
    break;

  case ACTION_STEP_BACK:
    if (m_timeCodePosition > 0)
      SeekToTimeCodeStamp(SEEK_RELATIVE, SEEK_BACKWARD);
    else
      g_application.m_pPlayer->Seek(false, false);
    return true;

  case ACTION_STEP_FORWARD:
    if (m_timeCodePosition > 0)
      SeekToTimeCodeStamp(SEEK_RELATIVE, SEEK_FORWARD);
    else
      g_application.m_pPlayer->Seek(true, false);
    return true;

  case ACTION_BIG_STEP_BACK:
    if (m_timeCodePosition > 0)
      SeekToTimeCodeStamp(SEEK_RELATIVE, SEEK_BACKWARD);
    else
      g_application.m_pPlayer->Seek(false, true);
    return true;

  case ACTION_BIG_STEP_FORWARD:
    if (m_timeCodePosition > 0)
      SeekToTimeCodeStamp(SEEK_RELATIVE, SEEK_FORWARD);
    else
      g_application.m_pPlayer->Seek(true, true);
    return true;

  case ACTION_NEXT_SCENE:
    if (g_application.m_pPlayer->SeekScene(true))
      g_infoManager.SetDisplayAfterSeek();
    return true;
    break;

  case ACTION_PREV_SCENE:
    if (g_application.m_pPlayer->SeekScene(false))
      g_infoManager.SetDisplayAfterSeek();
    return true;
    break;

  case ACTION_SHOW_OSD_TIME:
    m_bShowCurrentTime = !m_bShowCurrentTime;
    if(!m_bShowCurrentTime)
      g_infoManager.SetDisplayAfterSeek(0); //Force display off
    g_infoManager.SetShowTime(m_bShowCurrentTime);
    return true;
    break;

  case ACTION_SHOW_SUBTITLES:
    {
      if (!g_application.m_pPlayer || g_application.m_pPlayer->GetSubtitleCount() == 0)
        return true;

      bool subsOn = !g_application.m_pPlayer->GetSubtitleVisible();
      g_application.m_pPlayer->SetSubtitleVisible(subsOn);
      CStdString sub;
      if (subsOn)
      {
        SPlayerSubtitleStreamInfo info;
        g_application.m_pPlayer->GetSubtitleStreamInfo(CURRENT_STREAM, info);
        sub = (!info.name.empty()) ? CStdString(info.name) : g_localizeStrings.Get(231);
      }
      else
        sub = g_localizeStrings.Get(231);
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(287), sub, DisplTime, false, MsgTime);
    }
    return true;
    break;

  case ACTION_SHOW_INFO:
    {
      CGUIDialogFullScreenInfo* pDialog = (CGUIDialogFullScreenInfo*)g_windowManager.GetWindow(WINDOW_DIALOG_FULLSCREEN_INFO);
      if (pDialog)
      {
        CFileItem item(g_application.CurrentFileItem());
        pDialog->DoModal();
        return true;
      }
      break;
    }

  case ACTION_NEXT_SUBTITLE:
  case ACTION_CYCLE_SUBTITLE:
    {
      if (g_application.m_pPlayer->GetSubtitleCount() == 0)
        return true;

      int currentSub = g_application.m_pPlayer->GetSubtitle();
      bool currentSubVisible = true;

      if (g_application.m_pPlayer->GetSubtitleVisible())
      {
        if (++currentSub >= g_application.m_pPlayer->GetSubtitleCount())
        {
          currentSub = 0;
          if (action.GetID() == ACTION_NEXT_SUBTITLE)
          {
            g_application.m_pPlayer->SetSubtitleVisible(false);
            currentSubVisible = false;
          }
        }
        g_application.m_pPlayer->SetSubtitle(currentSub);
      }
      else if (action.GetID() == ACTION_NEXT_SUBTITLE)
      {
        g_application.m_pPlayer->SetSubtitleVisible(true);
      }

      CStdString sub;
      if (currentSubVisible)
      {
        SPlayerSubtitleStreamInfo info;
        g_application.m_pPlayer->GetSubtitleStreamInfo(currentSub, info);
        sub = (!info.name.empty()) ? CStdString(info.name) : g_localizeStrings.Get(231);
      }
      else
        sub = g_localizeStrings.Get(231);
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(287), sub, DisplTime, false, MsgTime);
    }
    return true;
    break;

  case ACTION_SUBTITLE_DELAY_MIN:
    {
      g_settings.m_currentVideoSettings.m_SubtitleDelay -= 0.1f;
      if (g_settings.m_currentVideoSettings.m_SubtitleDelay < -g_advancedSettings.m_videoSubsDelayRange)
        g_settings.m_currentVideoSettings.m_SubtitleDelay = -g_advancedSettings.m_videoSubsDelayRange;
      if (g_application.m_pPlayer)
        g_application.m_pPlayer->SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

      ShowSlider(action.GetID(), 22006, g_settings.m_currentVideoSettings.m_SubtitleDelay,
                 -g_advancedSettings.m_videoSubsDelayRange, 0.1f,
                 g_advancedSettings.m_videoSubsDelayRange);

      /* PLEX */
      CStdString delay;
      delay.Format("%.0f ms", g_settings.m_currentVideoSettings.m_SubtitleDelay * 1000);
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(52623), delay , DisplTime, false, MsgTime);
      /* END PLEX */

      return true;
      break;
    }
  case ACTION_SUBTITLE_DELAY_PLUS:
    {
      g_settings.m_currentVideoSettings.m_SubtitleDelay += 0.1f;
      if (g_settings.m_currentVideoSettings.m_SubtitleDelay > g_advancedSettings.m_videoSubsDelayRange)
        g_settings.m_currentVideoSettings.m_SubtitleDelay = g_advancedSettings.m_videoSubsDelayRange;
      if (g_application.m_pPlayer)
        g_application.m_pPlayer->SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

      ShowSlider(action.GetID(), 22006, g_settings.m_currentVideoSettings.m_SubtitleDelay,
                 -g_advancedSettings.m_videoSubsDelayRange, 0.1f,
                 g_advancedSettings.m_videoSubsDelayRange);

      /* PLEX */
      CStdString delay;
      delay.Format("%.0f ms", g_settings.m_currentVideoSettings.m_SubtitleDelay * 1000);
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(52623), delay , DisplTime, false, MsgTime);
      /* END PLEX */

      return true;
      break;
    }
  case ACTION_SUBTITLE_DELAY:
    ShowSlider(action.GetID(), 22006, g_settings.m_currentVideoSettings.m_SubtitleDelay,
                                      -g_advancedSettings.m_videoSubsDelayRange, 0.1f,
                                       g_advancedSettings.m_videoSubsDelayRange, true);
    return true;
    break;
  case ACTION_AUDIO_DELAY:
    ShowSlider(action.GetID(), 297, g_settings.m_currentVideoSettings.m_AudioDelay,
                                    -g_advancedSettings.m_videoAudioDelayRange, 0.025f,
                                     g_advancedSettings.m_videoAudioDelayRange, true);
    return true;
    break;
  case ACTION_AUDIO_DELAY_MIN:
    g_settings.m_currentVideoSettings.m_AudioDelay -= 0.025f;
    if (g_settings.m_currentVideoSettings.m_AudioDelay < -g_advancedSettings.m_videoAudioDelayRange)
      g_settings.m_currentVideoSettings.m_AudioDelay = -g_advancedSettings.m_videoAudioDelayRange;
    if (g_application.m_pPlayer)
      g_application.m_pPlayer->SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);

    ShowSlider(action.GetID(), 297, g_settings.m_currentVideoSettings.m_AudioDelay,
                                    -g_advancedSettings.m_videoAudioDelayRange, 0.025f,
                                     g_advancedSettings.m_videoAudioDelayRange);
    return true;
    break;
  case ACTION_AUDIO_DELAY_PLUS:
    g_settings.m_currentVideoSettings.m_AudioDelay += 0.025f;
    if (g_settings.m_currentVideoSettings.m_AudioDelay > g_advancedSettings.m_videoAudioDelayRange)
      g_settings.m_currentVideoSettings.m_AudioDelay = g_advancedSettings.m_videoAudioDelayRange;
    if (g_application.m_pPlayer)
      g_application.m_pPlayer->SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);

    ShowSlider(action.GetID(), 297, g_settings.m_currentVideoSettings.m_AudioDelay,
                                    -g_advancedSettings.m_videoAudioDelayRange, 0.025f,
                                     g_advancedSettings.m_videoAudioDelayRange);
    return true;
    break;
  case ACTION_AUDIO_NEXT_LANGUAGE:
    {
      if (!g_application.m_pPlayer || g_application.m_pPlayer->GetAudioStreamCount() == 1)
        return true;

      int currentAudio = g_application.m_pPlayer->GetAudioStream();

      if (++currentAudio >= g_application.m_pPlayer->GetAudioStreamCount())
        currentAudio = 0;
      g_application.m_pPlayer->SetAudioStream(currentAudio);    // Set the audio stream to the one selected
      SPlayerAudioStreamInfo info;
      g_application.m_pPlayer->GetAudioStreamInfo(g_settings.m_currentVideoSettings.m_AudioStream, info);
      CStdString aud = info.name;
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(460), aud, DisplTime, false, MsgTime);
      return true;
    }
    break;
  case REMOTE_0:
  case REMOTE_1:
  case REMOTE_2:
  case REMOTE_3:
  case REMOTE_4:
  case REMOTE_5:
  case REMOTE_6:
  case REMOTE_7:
  case REMOTE_8:
  case REMOTE_9:
    {
      if (g_application.CurrentFileItem().IsLiveTV())
      {
          CPVRChannelPtr channel;
        int iChannelNumber = -1;
        g_PVRManager.GetCurrentChannel(channel);

        if (action.GetID() == REMOTE_0)
        {
          iChannelNumber = g_PVRManager.GetPreviousChannel();
          if (iChannelNumber > 0)
            CLog::Log(LOGDEBUG, "switch to channel number %d", iChannelNumber);
          else
            CLog::Log(LOGDEBUG, "no previous channel number found");
        }
        else
        {
          int autoCloseTime = g_guiSettings.GetBool("pvrplayback.confirmchannelswitch") ? 0 : g_advancedSettings.m_iPVRNumericChannelSwitchTimeout;
          CStdString strChannel;
          strChannel.Format("%i", action.GetID() - REMOTE_0);
          if (CGUIDialogNumeric::ShowAndGetNumber(strChannel, g_localizeStrings.Get(19000), autoCloseTime) || autoCloseTime)
            iChannelNumber = atoi(strChannel.c_str());
        }

        if (iChannelNumber > 0 && iChannelNumber != channel->ChannelNumber())
        {
          CPVRChannelGroupPtr selectedGroup = g_PVRManager.GetPlayingGroup(channel->IsRadio());
          CFileItemPtr channel = selectedGroup->GetByChannelNumber(iChannelNumber);
          if (!channel || !channel->HasPVRChannelInfoTag())
            return false;

          OnAction(CAction(ACTION_CHANNEL_SWITCH, (float)iChannelNumber));
        }
      }
      else
      {
        ChangetheTimeCode(action.GetID());
      }
      return true;
    }
    break;

  case ACTION_ASPECT_RATIO:
    { // toggle the aspect ratio mode (only if the info is onscreen)
      if (m_bShowViewModeInfo)
      {
#ifdef HAS_VIDEO_PLAYBACK
        g_renderManager.SetViewMode(++g_settings.m_currentVideoSettings.m_ViewMode);
#endif
      }
      m_bShowViewModeInfo = true;
      m_dwShowViewModeTimeout = XbmcThreads::SystemClockMillis();
    }
    return true;
    break;
  case ACTION_SMALL_STEP_BACK:
    if (m_timeCodePosition > 0)
      SeekToTimeCodeStamp(SEEK_RELATIVE, SEEK_BACKWARD);
    else
    {
      int orgpos = (int)g_application.GetTime();
      int jumpsize = g_advancedSettings.m_videoSmallStepBackSeconds; // secs
      int setpos = (orgpos > jumpsize) ? orgpos - jumpsize : 0;
      g_application.SeekTime((double)setpos);
    }
    return true;
    break;
  case ACTION_SHOW_PLAYLIST:
    {
      CFileItem item(g_application.CurrentFileItem());
      if (item.HasPVRChannelInfoTag())
        g_windowManager.ActivateWindow(WINDOW_DIALOG_PVR_OSD_CHANNELS);
      else if (item.HasVideoInfoTag())
        g_windowManager.ActivateWindow(WINDOW_VIDEO_PLAYLIST);
      else if (item.HasMusicInfoTag())
        g_windowManager.ActivateWindow(WINDOW_MUSIC_PLAYLIST);
    }
    return true;
    break;
  case ACTION_ZOOM_IN:
    {
      g_settings.m_currentVideoSettings.m_CustomZoomAmount += 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomZoomAmount > 2.f)
        g_settings.m_currentVideoSettings.m_CustomZoomAmount = 2.f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 216, g_settings.m_currentVideoSettings.m_CustomZoomAmount, 0.5f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_ZOOM_OUT:
    {
      g_settings.m_currentVideoSettings.m_CustomZoomAmount -= 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomZoomAmount < 0.5f)
        g_settings.m_currentVideoSettings.m_CustomZoomAmount = 0.5f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 216, g_settings.m_currentVideoSettings.m_CustomZoomAmount, 0.5f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_INCREASE_PAR:
    {
      g_settings.m_currentVideoSettings.m_CustomPixelRatio += 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomPixelRatio > 2.f)
        g_settings.m_currentVideoSettings.m_CustomZoomAmount = 2.f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 217, g_settings.m_currentVideoSettings.m_CustomPixelRatio, 0.5f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_DECREASE_PAR:
    {
      g_settings.m_currentVideoSettings.m_CustomPixelRatio -= 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomZoomAmount < 0.5f)
        g_settings.m_currentVideoSettings.m_CustomPixelRatio = 0.5f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 217, g_settings.m_currentVideoSettings.m_CustomPixelRatio, 0.5f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_VSHIFT_UP:
    {
      g_settings.m_currentVideoSettings.m_CustomVerticalShift -= 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomVerticalShift < -2.0f)
        g_settings.m_currentVideoSettings.m_CustomVerticalShift = -2.0f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 225, g_settings.m_currentVideoSettings.m_CustomVerticalShift, -2.0f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_VSHIFT_DOWN:
    {
      g_settings.m_currentVideoSettings.m_CustomVerticalShift += 0.01f;
      if (g_settings.m_currentVideoSettings.m_CustomVerticalShift > 2.0f)
        g_settings.m_currentVideoSettings.m_CustomVerticalShift = 2.0f;
      g_settings.m_currentVideoSettings.m_ViewMode = VIEW_MODE_CUSTOM;
      g_renderManager.SetViewMode(VIEW_MODE_CUSTOM);
      ShowSlider(action.GetID(), 225, g_settings.m_currentVideoSettings.m_CustomVerticalShift, -2.0f, 0.1f, 2.0f);
    }
    return true;
    break;
  case ACTION_SUBTITLE_VSHIFT_UP:
    {
      RESOLUTION_INFO res_info = g_graphicsContext.GetResInfo();
      int subalign = g_guiSettings.GetInt("subtitles.align");
      if ((subalign == SUBTITLE_ALIGN_BOTTOM_OUTSIDE) || (subalign == SUBTITLE_ALIGN_TOP_INSIDE))
      {
        res_info.iSubtitles ++;
        if (res_info.iSubtitles >= res_info.iHeight)
          res_info.iSubtitles = res_info.iHeight - 1;

        ShowSlider(action.GetID(), 274, (float) res_info.iHeight - res_info.iSubtitles, 0.0f, 1.0f, (float) res_info.iHeight);
      }
      else
      {
        res_info.iSubtitles --;
        if (res_info.iSubtitles < 0)
          res_info.iSubtitles = 0;

        if (subalign == SUBTITLE_ALIGN_MANUAL)
          ShowSlider(action.GetID(), 274, (float) res_info.iSubtitles, 0.0f, 1.0f, (float) res_info.iHeight);
        else
          ShowSlider(action.GetID(), 274, (float) res_info.iSubtitles - res_info.iHeight, (float) -res_info.iHeight, -1.0f, 0.0f);
      }
      g_graphicsContext.SetResInfo(g_graphicsContext.GetVideoResolution(), res_info);
      break;
    }
  case ACTION_SUBTITLE_VSHIFT_DOWN:
    {
      RESOLUTION_INFO res_info =  g_graphicsContext.GetResInfo();
      int subalign = g_guiSettings.GetInt("subtitles.align");
      if ((subalign == SUBTITLE_ALIGN_BOTTOM_OUTSIDE) || (subalign == SUBTITLE_ALIGN_TOP_INSIDE))
      {
        res_info.iSubtitles--;
        if (res_info.iSubtitles < 0)
          res_info.iSubtitles = 0;

        ShowSlider(action.GetID(), 274, (float) res_info.iHeight - res_info.iSubtitles, 0.0f, 1.0f, (float) res_info.iHeight);
      }
      else
      {
        res_info.iSubtitles++;
        if (res_info.iSubtitles >= res_info.iHeight)
          res_info.iSubtitles = res_info.iHeight - 1;

        if (subalign == SUBTITLE_ALIGN_MANUAL)
          ShowSlider(action.GetID(), 274, (float) res_info.iSubtitles, 0.0f, 1.0f, (float) res_info.iHeight);
        else
          ShowSlider(action.GetID(), 274, (float) res_info.iSubtitles - res_info.iHeight, (float) -res_info.iHeight, -1.0f, 0.0f);
      }
      g_graphicsContext.SetResInfo(g_graphicsContext.GetVideoResolution(), res_info);
      break;
    }
  case ACTION_SUBTITLE_ALIGN:
    {
      RESOLUTION_INFO res_info = g_graphicsContext.GetResInfo();
      int subalign = g_guiSettings.GetInt("subtitles.align");

      subalign++;
      if (subalign > SUBTITLE_ALIGN_TOP_OUTSIDE)
        subalign = SUBTITLE_ALIGN_MANUAL;

      res_info.iSubtitles = res_info.iHeight - 1;

      g_guiSettings.SetInt("subtitles.align", subalign);
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info,
                                            g_localizeStrings.Get(21460),
                                 g_localizeStrings.Get(21461 + subalign), 
                                            TOAST_DISPLAY_TIME, false);
      g_graphicsContext.SetResInfo(g_graphicsContext.GetVideoResolution(), res_info);
      break;
    }
  case ACTION_VOLAMP_UP:
  case ACTION_VOLAMP_DOWN:
    {
      // Don't allow change with passthrough audio
      if (g_application.m_pPlayer->IsPassthrough())
      {
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning,
                                              g_localizeStrings.Get(660),
                                              g_localizeStrings.Get(29802),
                                              TOAST_DISPLAY_TIME, false);
        return false;
      }

      float sliderMax = VOLUME_DRC_MAXIMUM / 100.0f;
      float sliderMin = VOLUME_DRC_MINIMUM / 100.0f;

      if (action.GetID() == ACTION_VOLAMP_UP)
        g_settings.m_currentVideoSettings.m_VolumeAmplification += 1.0f;
      else
        g_settings.m_currentVideoSettings.m_VolumeAmplification -= 1.0f;

      g_settings.m_currentVideoSettings.m_VolumeAmplification =
        std::max(std::min(g_settings.m_currentVideoSettings.m_VolumeAmplification, sliderMax), sliderMin);

      if (g_application.m_pPlayer)
        g_application.m_pPlayer->SetDynamicRangeCompression((long)(g_settings.m_currentVideoSettings.m_VolumeAmplification * 100));

      ShowSlider(action.GetID(), 660, g_settings.m_currentVideoSettings.m_VolumeAmplification, sliderMin, 1.0f, sliderMax);

      break;
    }
  case ACTION_PREVIOUS_CHANNELGROUP:
    {
      if (g_application.CurrentFileItem().HasPVRChannelInfoTag())
        ChangetheTVGroup(false);
      return true;
    }
  case ACTION_NEXT_CHANNELGROUP:
    {
      if (g_application.CurrentFileItem().HasPVRChannelInfoTag())
        ChangetheTVGroup(true);
      return true;
    }
  default:
      break;
  }

  return CGUIWindow::OnAction(action);
}

void CGUIWindowFullScreen::ClearBackground()
{
  if (g_renderManager.IsVideoLayer())
#ifdef HAS_IMXVPU
    g_graphicsContext.Clear((16 << 16)|(8 << 8)|16);
#else
    g_graphicsContext.Clear(0);
#endif
}

void CGUIWindowFullScreen::OnWindowLoaded()
{
  CGUIWindow::OnWindowLoaded();
  // override the clear colour - we must never clear fullscreen
  m_clearBackground = 0;

  CGUIProgressControl* pProgress = (CGUIProgressControl*)GetControl(CONTROL_PROGRESS);
  if(pProgress)
  {
    if( pProgress->GetInfo() == 0 || pProgress->GetVisibleCondition() == 0)
    {
      pProgress->SetInfo(PLAYER_PROGRESS);
      pProgress->SetVisibleCondition("player.displayafterseek");
      pProgress->SetVisible(true);
    }
  }

  CGUILabelControl* pLabel = (CGUILabelControl*)GetControl(LABEL_BUFFERING);
  if(pLabel && pLabel->GetVisibleCondition() == 0)
  {
    pLabel->SetVisibleCondition("player.caching");
    pLabel->SetVisible(true);
  }

  pLabel = (CGUILabelControl*)GetControl(LABEL_CURRENT_TIME);
  if(pLabel && pLabel->GetVisibleCondition() == 0)
  {
    pLabel->SetVisibleCondition("player.displayafterseek");
    pLabel->SetVisible(true);
    pLabel->SetLabel("$INFO(VIDEOPLAYER.TIME) / $INFO(VIDEOPLAYER.DURATION)");
  }

  m_showCodec.Parse("player.showcodec", GetID());

  FillInTVGroups();
}

/* PLEX */
void CGUIWindowFullScreen::createOverlays()
{
  // VEVO Overlay handling
  if (g_application.CurrentFileItemPtr() && g_application.CurrentFileItemPtr()->m_overlayItems.size())
  {
    for (int i=0; i<g_application.CurrentFileItemPtr()->m_overlayItems.size(); i++)
    {
      CFileItemPtr overlayItem = g_application.CurrentFileItemPtr()->m_overlayItems[i];

      // setup the overlay image path
      CTextureInfo overlayInfo;
      overlayInfo.filename = overlayItem->GetPath();

      // compute the overlay position,
      // we're lucky rendering into 720p surface makes UI pixels -> physical pixels ratio to be 1:1
      int x1, y1;
      int frameBufferWidth = 1280;
      int frameBufferHeight = 720;

      // x1
      if (overlayItem->GetProperty("alignHorizontal").asString() == "left")
        x1 = overlayItem->GetProperty("marginLeft").asInteger();
      else if (overlayItem->GetProperty("alignHorizontal").asString() == "right")
        x1 = frameBufferWidth - overlayItem->GetProperty("width").asInteger() - overlayItem->GetProperty("marginRight").asInteger();
      else if (overlayItem->GetProperty("alignHorizontal").asString() == "center")
        x1 = frameBufferWidth - overlayItem->GetProperty("width").asInteger();

      // y1
      if (overlayItem->GetProperty("alignVertical").asString() == "top")
        y1 = overlayItem->GetProperty("marginTop").asInteger();
      else if (overlayItem->GetProperty("alignVertical").asString() == "bottom")
        y1 = frameBufferHeight - overlayItem->GetProperty("height").asInteger() - overlayItem->GetProperty("marginBottom").asInteger();
      else if (overlayItem->GetProperty("alignVertical").asString() == "center")
        y1 = frameBufferHeight  - overlayItem->GetProperty("height").asInteger();

      // width & height
      int w = overlayItem->GetProperty("width").asInteger();
      int h = overlayItem->GetProperty("height").asInteger();

      // add the overlay control
      CGUIControl *overlayControl = new CGUIImage(GetID(), CONTROL_OVERLAY_START + i,
                                                  x1,
                                                  y1,
                                                  w,
                                                  h,
                                                  overlayInfo, 1);
      AddControl(overlayControl);
    }
  }
}

void CGUIWindowFullScreen::deleteOverlays()
{
  // remove all the overlays controls
  for (int i = 0; i < 100; i++)
  {
    CGUIControl *overlayControl = (CGUIControl*)GetControl(CONTROL_OVERLAY_START + i);
    if (overlayControl)
    {
      RemoveControl(overlayControl);
      overlayControl->FreeResources();
    }
    else
      break;
  }
}

/* END PLEX */

bool CGUIWindowFullScreen::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_WINDOW_INIT:
    {
      /* PLEX */
      deleteOverlays();
      /* END PLEX */

      // check whether we've come back here from a window during which time we've actually
      // stopped playing videos
      if (message.GetParam1() == WINDOW_INVALID && !g_application.IsPlayingVideo())
      { // why are we here if nothing is playing???
        g_windowManager.PreviousWindow();
        return true;
      }
      g_infoManager.SetShowInfo(false);
      g_infoManager.SetShowCodec(false);
      m_bShowCurrentTime = false;
      m_bGroupSelectShow = false;
      g_infoManager.SetDisplayAfterSeek(0); // Make sure display after seek is off.

      // switch resolution
      g_graphicsContext.SetFullScreenVideo(true);

#ifdef HAS_VIDEO_PLAYBACK
      // make sure renderer is uptospeed
      g_renderManager.Update();
#endif
      // now call the base class to load our windows
      CGUIWindow::OnMessage(message);

      m_bShowViewModeInfo = false;

      /* PLEX */
      createOverlays();
      /* END PLEX */

      return true;
    }
  case GUI_MSG_WINDOW_DEINIT:
    {
      CGUIDialog *pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_OSD_TELETEXT);
      if (pDialog) pDialog->Close(true);
      CGUIDialogSlider *slider = (CGUIDialogSlider *)g_windowManager.GetWindow(WINDOW_DIALOG_SLIDER);
      if (slider) slider->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_VIDEO_OSD);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_FULLSCREEN_INFO);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_OSD_CHANNELS);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_OSD_GUIDE);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_OSD_DIRECTOR);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog *)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_OSD_CUTTER);
      if (pDialog) pDialog->Close(true);

      /* PLEX */
      pDialog = (CGUIDialog*)g_windowManager.GetWindow(WINDOW_DIALOG_PLEX_AUDIO_PICKER);
      if (pDialog) pDialog->Close(true);
      pDialog = (CGUIDialog*)g_windowManager.GetWindow(WINDOW_DIALOG_PLEX_SUBTITLE_PICKER);
      if (pDialog) pDialog->Close(true);
      /* END PLEX */


      CGUIWindow::OnMessage(message);

      g_settings.Save();

      CSingleLock lock (g_graphicsContext);
      g_graphicsContext.SetFullScreenVideo(false);
      lock.Leave();

#ifdef HAS_VIDEO_PLAYBACK
      // make sure renderer is uptospeed
      g_renderManager.Update();
      g_renderManager.FrameFinish();
#endif

      /* PLEX */
      deleteOverlays();
      /* END PLEX */

      return true;
    }
  case GUI_MSG_CLICKED:
    {
      unsigned int iControl = message.GetSenderId();
      if (iControl == CONTROL_GROUP_CHOOSER && g_PVRManager.IsStarted())
      {
        // Get the currently selected label of the Select button
        CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iControl);
        OnMessage(msg);
        CStdString strLabel = msg.GetLabel();

        CPVRChannelPtr playingChannel;
        if (g_PVRManager.GetCurrentChannel(playingChannel))
        {
          CPVRChannelGroupPtr selectedGroup = g_PVRChannelGroups->Get(playingChannel->IsRadio())->GetByName(strLabel);
          if (selectedGroup)
          {
            g_PVRManager.SetPlayingGroup(selectedGroup);
            CLog::Log(LOGDEBUG, "%s - switched to group '%s'", __FUNCTION__, selectedGroup->GroupName().c_str());

            if (!selectedGroup->IsGroupMember(*playingChannel))
            {
              CLog::Log(LOGDEBUG, "%s - channel '%s' is not a member of '%s', switching to channel 1 of the new group",
                  __FUNCTION__, playingChannel->ChannelName().c_str(), selectedGroup->GroupName().c_str());
              CFileItemPtr switchChannel = selectedGroup->GetByChannelNumber(1);

              if (switchChannel && switchChannel->HasPVRChannelInfoTag())
                OnAction(CAction(ACTION_CHANNEL_SWITCH, (float) switchChannel->GetPVRChannelInfoTag()->ChannelNumber()));
              else
              {
                CLog::Log(LOGERROR, "%s - cannot find channel '1' in group %s", __FUNCTION__, selectedGroup->GroupName().c_str());
                CApplicationMessenger::Get().MediaStop(false);
              }
            }
          }
          else
          {
            CLog::Log(LOGERROR, "%s - could not switch to group '%s'", __FUNCTION__, selectedGroup->GroupName().c_str());
            CApplicationMessenger::Get().MediaStop(false);
          }
        }
        else
        {
          CLog::Log(LOGERROR, "%s - cannot find the current channel", __FUNCTION__);
          CApplicationMessenger::Get().MediaStop(false);
        }

        // hide the control and reset focus
        m_bGroupSelectShow = false;
        SET_CONTROL_HIDDEN(CONTROL_GROUP_CHOOSER);
//      SET_CONTROL_FOCUS(0, 0);

        return true;
      }
      break;
    }
  case GUI_MSG_SETFOCUS:
  case GUI_MSG_LOSTFOCUS:
    if (message.GetSenderId() != WINDOW_FULLSCREEN_VIDEO) return true;
    break;
  }

  return CGUIWindow::OnMessage(message);
}

EVENT_RESULT CGUIWindowFullScreen::OnMouseEvent(const CPoint &point, const CMouseEvent &event)
{
  if (event.m_id == ACTION_MOUSE_RIGHT_CLICK)
  { // no control found to absorb this click - go back to GUI
    OnAction(CAction(ACTION_SHOW_GUI));
    return EVENT_RESULT_HANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_UP)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_FORWARD, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_DOWN)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_BACK, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  if (event.m_id == ACTION_GESTURE_NOTIFY)
    return EVENT_RESULT_UNHANDLED;
  if (event.m_id != ACTION_MOUSE_MOVE || event.m_offsetX || event.m_offsetY)
  { // some other mouse action has occurred - bring up the OSD
    // if it is not already running
    CGUIDialogVideoOSD *pOSD = (CGUIDialogVideoOSD *)g_windowManager.GetWindow(WINDOW_DIALOG_VIDEO_OSD);
    if (pOSD && !pOSD->IsDialogRunning())
    {
      pOSD->SetAutoClose(3000);
      pOSD->DoModal();
    }
    return EVENT_RESULT_HANDLED;
  }
  return EVENT_RESULT_UNHANDLED;
}

void CGUIWindowFullScreen::FrameMove()
{
  if (g_application.GetPlaySpeed() != 1)
    g_infoManager.SetDisplayAfterSeek();
  if (m_bShowCurrentTime)
    g_infoManager.SetDisplayAfterSeek();

  if (!g_application.m_pPlayer) return;

#ifndef __PLEX__
  if( g_application.m_pPlayer->IsCaching() )
  {
    g_infoManager.SetDisplayAfterSeek(0); //Make sure these stuff aren't visible now
  }
#endif

  //------------------------
  m_showCodec.Update();
  if (m_showCodec)
  {
    // show audio codec info
    CStdString strAudio, strVideo, strGeneral;
    g_application.m_pPlayer->GetAudioInfo(strAudio);
    {
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW1);
      msg.SetLabel(strAudio);
      OnMessage(msg);
    }
    // show video codec info
    g_application.m_pPlayer->GetVideoInfo(strVideo);
    {
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW2);
      msg.SetLabel(strVideo);
      OnMessage(msg);
    }
    // show general info
    g_application.m_pPlayer->GetGeneralInfo(strGeneral);
    {
      CStdString strGeneralFPS;
#if defined(TARGET_DARWIN)
      // We show CPU usage for the entire process, as it's arguably more useful.
      double dCPU = m_resourceCounter.GetCPUUsage();
      CStdString strCores;
      strCores.Format("cpu:%.0f%%", dCPU);
#else
      CStdString strCores = g_cpuInfo.GetCoresUsageString();
#endif
      int    missedvblanks;
      double refreshrate;
      double clockspeed;
      CStdString strClock;

      if (g_VideoReferenceClock.GetClockInfo(missedvblanks, clockspeed, refreshrate))
        strClock.Format("S( refresh:%.3f missed:%i speed:%+.3f%% %s )"
                                       , refreshrate
                                       , missedvblanks
                                       , clockspeed - 100.0
                                       , g_renderManager.GetVSyncState().c_str());

      /* PLEX */
      CStdString plexInfo;
      CFileItemPtr f = g_application.CurrentFileItemPtr();
      if (f)
      {
        CStdString transcodeInfo = "direct-play";
        if (f->GetProperty("plexDidTranscode").asBoolean())
          transcodeInfo.Format("transcoded");
        CPlexServerPtr s = g_plexApplication.serverManager->FindByUUID(f->GetProperty("plexserver").asString());
        if (s)
          plexInfo.Format("P( server:%s %s )", s->GetName().c_str(), transcodeInfo);
      }

      /*END PLEX*/

      strGeneralFPS.Format("%s\nW( %s ) %s\n%s"
                                          , strGeneral.c_str()
                                          , strCores.c_str(), plexInfo.c_str(), strClock.c_str() );

      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW3);
      msg.SetLabel(strGeneralFPS);
      OnMessage(msg);
    }
  }
  //----------------------
  // ViewMode Information
  //----------------------
  if (m_bShowViewModeInfo && XbmcThreads::SystemClockMillis() - m_dwShowViewModeTimeout > 2500)
  {
    m_bShowViewModeInfo = false;
  }
  if (m_bShowViewModeInfo)
  {
    RESOLUTION_INFO res = g_graphicsContext.GetResInfo();

    {
      // get the "View Mode" string
      CStdString strTitle = g_localizeStrings.Get(629);
      CStdString strMode = g_localizeStrings.Get(630 + g_settings.m_currentVideoSettings.m_ViewMode);
      CStdString strInfo;
      strInfo.Format("%s : %s", strTitle.c_str(), strMode.c_str());
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW1);
      msg.SetLabel(strInfo);
      OnMessage(msg);
    }
    // show sizing information
    SPlayerVideoStreamInfo info;
    g_application.m_pPlayer->GetVideoStreamInfo(info);
    {
      // Splitres scaling factor
      float xscale = (float)res.iScreenWidth  / (float)res.iWidth;
      float yscale = (float)res.iScreenHeight / (float)res.iHeight;

      CStdString strSizing;
      strSizing.Format(g_localizeStrings.Get(245),
                       (int)info.SrcRect.Width(),
                       (int)info.SrcRect.Height(),
                       (int)(info.DestRect.Width() * xscale),
                       (int)(info.DestRect.Height() * yscale),
                       g_settings.m_fZoomAmount,
                       info.videoAspectRatio*g_settings.m_fPixelRatio, 
                       g_settings.m_fPixelRatio,
                       g_settings.m_fVerticalShift);
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW2);
      msg.SetLabel(strSizing);
      OnMessage(msg);
    }
    // show resolution information
    {
      CStdString strStatus;
      if (g_Windowing.IsFullScreen())
        strStatus.Format("%s %ix%i@%.2fHz - %s",
          g_localizeStrings.Get(13287), 
          res.iScreenWidth,
          res.iScreenHeight,
          res.fRefreshRate,
          g_localizeStrings.Get(244));
      else
        strStatus.Format("%s %ix%i - %s",
          g_localizeStrings.Get(13287), 
          res.iScreenWidth,
          res.iScreenHeight, 
          g_localizeStrings.Get(242));

      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW3);
      msg.SetLabel(strStatus);
      OnMessage(msg);
    }
  }

  if (m_timeCodeShow && m_timeCodePosition != 0)
  {
    if ( (XbmcThreads::SystemClockMillis() - m_timeCodeTimeout) >= 2500)
    {
      m_timeCodeShow = false;
      m_timeCodePosition = 0;
    }
    CStdString strDispTime = "00:00:00";

    CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW1);

    for (int pos = 7, i = m_timeCodePosition; pos >= 0 && i > 0; pos--)
    {
      if (strDispTime[pos] != ':')
      {
        i -= 1;
        strDispTime[pos] = (char)m_timeCodeStamp[i] + '0';
      }
    }

    strDispTime += "/" + g_infoManager.GetDuration(TIME_FORMAT_HH_MM_SS) + " [" + g_infoManager.GetCurrentPlayTime(TIME_FORMAT_HH_MM_SS) + "]"; // duration [ time ]
    msg.SetLabel(strDispTime);
    OnMessage(msg);
  }

  if (m_showCodec || m_bShowViewModeInfo)
  {
    SET_CONTROL_VISIBLE(LABEL_ROW1);
    SET_CONTROL_VISIBLE(LABEL_ROW2);
    SET_CONTROL_VISIBLE(LABEL_ROW3);
    SET_CONTROL_VISIBLE(BLUE_BAR);
    SET_CONTROL_HIDDEN(CONTROL_GROUP_CHOOSER);
  }
  else if (m_timeCodeShow)
  {
    SET_CONTROL_VISIBLE(LABEL_ROW1);
    SET_CONTROL_HIDDEN(LABEL_ROW2);
    SET_CONTROL_HIDDEN(LABEL_ROW3);
    SET_CONTROL_VISIBLE(BLUE_BAR);
    SET_CONTROL_HIDDEN(CONTROL_GROUP_CHOOSER);
  }
  else if (m_bGroupSelectShow)
  {
    SET_CONTROL_HIDDEN(LABEL_ROW1);
    SET_CONTROL_HIDDEN(LABEL_ROW2);
    SET_CONTROL_HIDDEN(LABEL_ROW3);
    SET_CONTROL_HIDDEN(BLUE_BAR);
    SET_CONTROL_VISIBLE(CONTROL_GROUP_CHOOSER);
  }
  else
  {
    SET_CONTROL_HIDDEN(LABEL_ROW1);
    SET_CONTROL_HIDDEN(LABEL_ROW2);
    SET_CONTROL_HIDDEN(LABEL_ROW3);
    SET_CONTROL_HIDDEN(BLUE_BAR);
    SET_CONTROL_HIDDEN(CONTROL_GROUP_CHOOSER);
  }

  g_renderManager.FrameMove();
}

void CGUIWindowFullScreen::Process(unsigned int currentTime, CDirtyRegionList &dirtyregion)
{
  if (g_renderManager.IsGuiLayer())
    MarkDirtyRegion();

  CGUIWindow::Process(currentTime, dirtyregion);

  // TODO: This isn't quite optimal - ideally we'd only be dirtying up the actual video render rect
  //       which is probably the job of the renderer as it can more easily track resizing etc.
  m_renderRegion.SetRect(0, 0, (float)g_graphicsContext.GetWidth(), (float)g_graphicsContext.GetHeight());
}

void CGUIWindowFullScreen::Render()
{
  g_graphicsContext.SetRenderingResolution(g_graphicsContext.GetVideoResolution(), false);
  g_renderManager.Render(true, 0, 255);
  g_graphicsContext.SetRenderingResolution(m_coordsRes, m_needsScaling);
  CGUIWindow::Render();
}

void CGUIWindowFullScreen::RenderEx()
{
  CGUIWindow::RenderEx();
  g_graphicsContext.SetRenderingResolution(g_graphicsContext.GetVideoResolution(), false);
#ifdef HAS_VIDEO_PLAYBACK
  g_renderManager.Render(false, 0, 255, false);
  g_renderManager.FrameFinish();
#endif
}

void CGUIWindowFullScreen::ChangetheTimeCode(int remote)
{
  if (remote >= REMOTE_0 && remote <= REMOTE_9)
  {
    m_timeCodeShow = true;
    m_timeCodeTimeout = XbmcThreads::SystemClockMillis();

    if (m_timeCodePosition < 6)
      m_timeCodeStamp[m_timeCodePosition++] = remote - REMOTE_0;
    else
    {
      // rotate around
      for (int i = 0; i < 5; i++)
        m_timeCodeStamp[i] = m_timeCodeStamp[i+1];
      m_timeCodeStamp[5] = remote - REMOTE_0;
    }
  }
}

void CGUIWindowFullScreen::SeekToTimeCodeStamp(SEEK_TYPE type, SEEK_DIRECTION direction)
{
  double total = GetTimeCodeStamp();
  if (type == SEEK_RELATIVE)
    total = g_application.GetTime() + (((direction == SEEK_FORWARD) ? 1 : -1) * total);

  if (total < g_application.GetTotalTime())
    g_application.SeekTime(total);

  m_timeCodePosition = 0;
  m_timeCodeShow = false;
}

double CGUIWindowFullScreen::GetTimeCodeStamp()
{
  // Convert the timestamp into an integer
  int tot = 0;
  for (int i = 0; i < m_timeCodePosition; i++)
    tot = tot * 10 + m_timeCodeStamp[i];

  // Interpret result as HHMMSS
  int s = tot % 100; tot /= 100;
  int m = tot % 100; tot /= 100;
  int h = tot % 100;
  return h * 3600 + m * 60 + s;
}

void CGUIWindowFullScreen::SeekChapter(int iChapter)
{
  g_application.m_pPlayer->SeekChapter(iChapter);

  // Make sure gui items are visible.
  g_infoManager.SetDisplayAfterSeek();
}

void CGUIWindowFullScreen::ShowSlider(int action, int label, float value, float min, float delta, float max, bool modal)
{
  m_sliderAction = action;
  if (modal)
    CGUIDialogSlider::ShowAndGetInput(g_localizeStrings.Get(label), value, min, delta, max, this);
  else
    CGUIDialogSlider::Display(label, value, min, delta, max, this);
}

void CGUIWindowFullScreen::OnSliderChange(void *data, CGUISliderControl *slider)
{
  if (!slider)
    return;

  if (m_sliderAction == ACTION_ZOOM_OUT || m_sliderAction == ACTION_ZOOM_IN ||
      m_sliderAction == ACTION_INCREASE_PAR || m_sliderAction == ACTION_DECREASE_PAR ||
      m_sliderAction == ACTION_VSHIFT_UP || m_sliderAction == ACTION_VSHIFT_DOWN ||
      m_sliderAction == ACTION_SUBTITLE_VSHIFT_UP || m_sliderAction == ACTION_SUBTITLE_VSHIFT_DOWN)
  {
    CStdString strValue;
    strValue.Format("%1.2f",slider->GetFloatValue());
    slider->SetTextValue(strValue);
  }
  else if (m_sliderAction == ACTION_VOLAMP_UP || m_sliderAction == ACTION_VOLAMP_DOWN)
    slider->SetTextValue(CGUIDialogAudioSubtitleSettings::FormatDecibel(slider->GetFloatValue(), 1.0f));
  else
    slider->SetTextValue(CGUIDialogAudioSubtitleSettings::FormatDelay(slider->GetFloatValue(), 0.025f));

  if (g_application.m_pPlayer)
  {
    if (m_sliderAction == ACTION_AUDIO_DELAY)
    {
      g_settings.m_currentVideoSettings.m_AudioDelay = slider->GetFloatValue();
      g_application.m_pPlayer->SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);
    }
    else if (m_sliderAction == ACTION_SUBTITLE_DELAY)
    {
      g_settings.m_currentVideoSettings.m_SubtitleDelay = slider->GetFloatValue();
      g_application.m_pPlayer->SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);
    }
  }
}

void CGUIWindowFullScreen::FillInTVGroups()
{
  if (!g_PVRManager.IsStarted())
    return;

  CGUIMessage msgReset(GUI_MSG_LABEL_RESET, GetID(), CONTROL_GROUP_CHOOSER);
  g_windowManager.SendMessage(msgReset);

  const CPVRChannelGroups *groups = g_PVRChannelGroups->Get(g_PVRManager.IsPlayingRadio());
  if (groups)
    groups->FillGroupsGUI(GetID(), CONTROL_GROUP_CHOOSER);
}

void CGUIWindowFullScreen::ChangetheTVGroup(bool next)
{
  if (!g_PVRManager.IsStarted())
    return;

  CGUISelectButtonControl* pButton = (CGUISelectButtonControl*)GetControl(CONTROL_GROUP_CHOOSER);
  if (!pButton)
    return;

  if (!m_bGroupSelectShow)
  {
    SET_CONTROL_VISIBLE(CONTROL_GROUP_CHOOSER);
    SET_CONTROL_FOCUS(CONTROL_GROUP_CHOOSER, 0);

    // fire off an event that we've pressed this button...
    OnAction(CAction(ACTION_SELECT_ITEM));

    m_bGroupSelectShow = true;
  }
  else
  {
    if (next)
      pButton->OnRight();
    else
      pButton->OnLeft();
  }
}

void CGUIWindowFullScreen::ToggleOSD()
{
  CGUIDialogVideoOSD *pOSD = (CGUIDialogVideoOSD *)g_windowManager.GetWindow(WINDOW_DIALOG_VIDEO_OSD);
  if (pOSD)
  {
    /* PLEX */
    CLog::Log(LOGDEBUG, "CGUIWindowFullScreen::Toggle OSD close: %s", pOSD->IsDialogRunning() ? "yes" : "no");
    /* END PLEX */
    if (pOSD->IsDialogRunning())
      pOSD->Close();
    else
      pOSD->DoModal();
  }

  MarkDirtyRegion();
}

void CGUIWindowFullScreen::TriggerOSD()
{
  CGUIDialogVideoOSD *pOSD = (CGUIDialogVideoOSD *)g_windowManager.GetWindow(WINDOW_DIALOG_VIDEO_OSD);
  if (pOSD && !pOSD->IsDialogRunning())
  {
    pOSD->SetAutoClose(3000);
    pOSD->DoModal();
  }
}
