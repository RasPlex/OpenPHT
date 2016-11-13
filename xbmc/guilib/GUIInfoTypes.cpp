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

#include "GUIInfoTypes.h"
#include "GUIInfoManager.h"
#include "addons/AddonManager.h"
#include "utils/log.h"
#include "LocalizeStrings.h"
#include "GUIColorManager.h"
#include "GUIListItem.h"
#include "utils/StringUtils.h"
#include "addons/Skin.h"

using ADDON::CAddonMgr;

CGUIInfoBool::CGUIInfoBool(bool value)
{
  m_value = value;
}

CGUIInfoBool::~CGUIInfoBool()
{
}

void CGUIInfoBool::Parse(const CStdString &expression, int context)
{
  if (expression == "true")
    m_value = true;
  else if (expression == "false")
    m_value = false;
  else
  {
    m_info = g_infoManager.Register(expression, context);
    Update();
  }
}

void CGUIInfoBool::Update(const CGUIListItem *item /*= NULL*/)
{
  if (m_info)
    m_value = m_info->Get(item);
}


CGUIInfoColor::CGUIInfoColor(uint32_t color)
{
  m_color = color;
  m_info = 0;
}

CGUIInfoColor &CGUIInfoColor::operator=(color_t color)
{
  m_color = color;
  m_info = 0;
  return *this;
}

CGUIInfoColor &CGUIInfoColor::operator=(const CGUIInfoColor &color)
{
  m_color = color.m_color;
  m_info = color.m_info;
  return *this;
}

bool CGUIInfoColor::Update()
{
  if (!m_info)
    return false; // no infolabel

  // Expand the infolabel, and then convert it to a color
  CStdString infoLabel(g_infoManager.GetLabel(m_info));
  color_t color = !infoLabel.empty() ? g_colorManager.GetColor(infoLabel.c_str()) : 0;
  if (m_color != color)
  {
    m_color = color;
    return true;
  }
  else
    return false;
}

void CGUIInfoColor::Parse(const CStdString &label, int context)
{
  // Check for the standard $INFO[] block layout, and strip it if present
  CStdString label2 = label;
  if (label.Equals("-", false))
    return;

  if (label.Left(5).Equals("$var[", false))
  {
    label2 = label.substr(5, label.length() - 6);
    m_info = g_infoManager.TranslateSkinVariableString(label2, context);
    if (!m_info)
      m_info = g_infoManager.RegisterSkinVariableString(g_SkinInfo->CreateSkinVariable(label2, context));
    return;
  }

  if (label.Left(6).Equals("$info[", false))
    label2 = label.substr(6, label.length()-7);

  m_info = g_infoManager.TranslateString(label2);
  if (!m_info)
    m_color = g_colorManager.GetColor(label);
}

CGUIInfoLabel::CGUIInfoLabel() : m_dirty(false)
{
}

CGUIInfoLabel::CGUIInfoLabel(const CStdString &label, const CStdString &fallback /*= ""*/, int context /*= 0*/) : m_dirty(false)
{
  SetLabel(label, fallback, context);
}

int CGUIInfoLabel::GetIntValue(int contextWindow) const
{
  CStdString label = GetLabel(contextWindow);
  if (!label.empty())
    return strtol(label.c_str(), NULL, 10);

  return 0;
}

void CGUIInfoLabel::SetLabel(const CStdString &label, const CStdString &fallback, int context /*= 0*/)
{
  m_fallback = fallback;
  Parse(label, context);
}

const CStdString &CGUIInfoLabel::GetLabel(int contextWindow, bool preferImage, CStdString *fallback /*= NULL*/) const
{
  bool needsUpdate = m_dirty;
  if (!m_info.empty())
  {
    for (std::vector<CInfoPortion>::const_iterator portion = m_info.begin(); portion != m_info.end(); ++portion)
    {
      if (portion->m_info)
      {
        CStdString infoLabel;
        if (preferImage)
          infoLabel = g_infoManager.GetImage(portion->m_info, contextWindow, fallback);
        if (infoLabel.empty())
          infoLabel = g_infoManager.GetLabel(portion->m_info, contextWindow, fallback);
        needsUpdate |= portion->NeedsUpdate(infoLabel);
      }
    }
  }
  else
    needsUpdate = !m_label.empty();

  return CacheLabel(needsUpdate);
}

const CStdString &CGUIInfoLabel::GetItemLabel(const CGUIListItem *item, bool preferImages, CStdString *fallback /*= NULL*/) const
{
  bool needsUpdate = m_dirty;
  if (item->IsFileItem() && !m_info.empty())
  {
    for (std::vector<CInfoPortion>::const_iterator portion = m_info.begin(); portion != m_info.end(); ++portion)
    {
      if (portion->m_info)
      {
        CStdString infoLabel;
        if (preferImages)
          infoLabel = g_infoManager.GetItemImage((const CFileItem *)item, portion->m_info, fallback);
        else
          infoLabel = g_infoManager.GetItemLabel((const CFileItem *)item, portion->m_info, fallback);
        needsUpdate |= portion->NeedsUpdate(infoLabel);
      }
    }
  }
  else
    needsUpdate = !m_label.empty();

  return CacheLabel(needsUpdate);
}

const CStdString &CGUIInfoLabel::CacheLabel(bool rebuild) const
{
  if (rebuild)
  {
    m_label.clear();
    for (std::vector<CInfoPortion>::const_iterator portion = m_info.begin(); portion != m_info.end(); ++portion)
      m_label += portion->Get();
    m_dirty = false;
  }
  if (m_label.empty())  // empty label, use the fallback
    return m_fallback;
  return m_label;
}

bool CGUIInfoLabel::IsEmpty() const
{
  return m_info.empty();
}

bool CGUIInfoLabel::IsConstant() const
{
  return m_info.empty() || (m_info.size() == 1 && m_info[0].m_info == 0);
}

bool CGUIInfoLabel::ReplaceSpecialKeywordReferences(const CStdString &strInput, const CStdString &strKeyword, const StringReplacerFunc &func, CStdString &strOutput)
{
  // replace all $strKeyword[value] with resolved strings
  std::string dollarStrPrefix = "$" + strKeyword + "[";

  size_t index = 0;
  size_t startPos;

  while ((startPos = strInput.find(dollarStrPrefix, index)) != std::string::npos)
  {
    size_t valuePos = startPos + dollarStrPrefix.size();
    size_t endPos = StringUtils::FindEndBracket(strInput, '[', ']', valuePos);
    if (endPos != std::string::npos)
    {
      if (index == 0)  // first occurrence?
        strOutput.clear();
      strOutput += strInput.substr(index, startPos - index);            // append part from the left side
      strOutput += func(strInput.substr(valuePos, endPos - valuePos));  // resolve and append value part
      index = endPos + 1;
    }
    else
    {
      // if closing bracket is missing, report error and leave incomplete reference in
      CLog::Log(LOGERROR, "Error parsing value - missing ']' in \"%s\"", strInput.c_str());
      break;
    }
  }

  if (index)  // if we replaced anything
  {
    strOutput += strInput.substr(index);  // append leftover from the right side
    return true;
  }

  return false;
}

bool CGUIInfoLabel::ReplaceSpecialKeywordReferences(CStdString &work, const CStdString &strKeyword, const StringReplacerFunc &func)
{
  CStdString output;
  if (ReplaceSpecialKeywordReferences(work, strKeyword, func, output))
  {
    work = output;
    return true;
  }
  return false;
}

CStdString LocalizeReplacer(const CStdString &str)
{
  CStdString replace = g_localizeStringsTemp.Get(atoi(str.c_str()));
  if (replace.empty())
    replace = g_localizeStrings.Get(atoi(str.c_str()));
  return replace;
}

CStdString AddonReplacer(const CStdString &str)
{
  // assumes "addon.id #####"
  size_t length = str.find(" ");
  CStdString id = str.substr(0, length);
  int stringid = atoi(str.substr(length + 1).c_str());
  return CAddonMgr::Get().GetString(id, stringid);
}

CStdString NumberReplacer(const CStdString &str)
{
  return str;
}

CStdString CGUIInfoLabel::ReplaceLocalize(const CStdString &label)
{
  CStdString work(label);
  ReplaceSpecialKeywordReferences(work, "LOCALIZE", LocalizeReplacer);
  ReplaceSpecialKeywordReferences(work, "NUMBER", NumberReplacer);
  return work;
}

CStdString CGUIInfoLabel::ReplaceAddonStrings(const CStdString &label)
{
  CStdString work(label);
  ReplaceSpecialKeywordReferences(work, "ADDON", AddonReplacer);
  return work;
}

enum EINFOFORMAT { NONE = 0, FORMATINFO, FORMATESCINFO, FORMATVAR, FORMATESCVAR };

typedef struct
{
  const char *str;
  EINFOFORMAT  val;
} infoformat;

const static infoformat infoformatmap[] = {{ "$INFO[",    FORMATINFO},
                                           { "$ESCINFO[", FORMATESCINFO},
                                           { "$VAR[",     FORMATVAR},
                                           { "$ESCVAR[",  FORMATESCVAR}};

void CGUIInfoLabel::Parse(const CStdString &label, int context)
{
  m_info.clear();
  m_dirty = true;
  // Step 1: Replace all $LOCALIZE[number] with the real string
  CStdString work = ReplaceLocalize(label);
  // Step 2: Replace all $ADDON[id number] with the real string
  work = ReplaceAddonStrings(work);
  // Step 3: Find all $INFO[info,prefix,postfix] blocks
  EINFOFORMAT format;
  do
  {
    format = NONE;
    size_t pos1 = work.size();
    size_t pos2;
    size_t len = 0;
    for (size_t i = 0; i < sizeof(infoformatmap) / sizeof(infoformat); i++)
    {
      pos2 = work.find(infoformatmap[i].str);
      if (pos2 != std::string::npos && pos2 < pos1)
      {
        pos1 = pos2;
        len = strlen(infoformatmap[i].str);
        format = infoformatmap[i].val;
      }
    }

    if (format != NONE)
    {
      if (pos1 > 0)
        m_info.push_back(CInfoPortion(0, work.substr(0, pos1), ""));

      pos2 = StringUtils::FindEndBracket(work, '[', ']', pos1 + len);
      if (pos2 != std::string::npos)
      {
        // decipher the block
        CStdString block = work.substr(pos1 + len, pos2 - pos1 - len);
        CStdStringArray params;
        StringUtils::SplitString(block, ",", params);
        if (!params.empty())
        {
          int info;
          if (format == FORMATVAR || format == FORMATESCVAR)
          {
            info = g_infoManager.TranslateSkinVariableString(params[0], context);
            if (info == 0)
              info = g_infoManager.RegisterSkinVariableString(g_SkinInfo->CreateSkinVariable(params[0], context));
            if (info == 0) // skinner didn't define this conditional label!
              CLog::Log(LOGWARNING, "Label Formating: $VAR[%s] is not defined", params[0].c_str());
          }
          else
            info = g_infoManager.TranslateString(params[0]);
          CStdString prefix, postfix;
          if (params.size() > 1)
            prefix = params[1];
          if (params.size() > 2)
            postfix = params[2];
          m_info.push_back(CInfoPortion(info, prefix, postfix, format == FORMATESCINFO || format == FORMATESCVAR));
        }
        // and delete it from our work string
        work = work.substr(pos2 + 1);
      }
      else
      {
        CLog::Log(LOGERROR, "Error parsing label - missing ']' in \"%s\"", label.c_str());
        return;
      }
    }
  }
  while (format != NONE);

  if (!work.empty())
    m_info.push_back(CInfoPortion(0, work, ""));
}

CGUIInfoLabel::CInfoPortion::CInfoPortion(int info, const CStdString &prefix, const CStdString &postfix, bool escaped /*= false */):
  m_prefix(prefix),
  m_postfix(postfix)
{
  m_info = info;
  m_escaped = escaped;
  // filter our prefix and postfix for comma's
  m_prefix.Replace("$COMMA", ",");
  m_postfix.Replace("$COMMA", ",");
  m_prefix.Replace("$LBRACKET", "["); m_prefix.Replace("$RBRACKET", "]");
  m_postfix.Replace("$LBRACKET", "["); m_postfix.Replace("$RBRACKET", "]");
}

bool CGUIInfoLabel::CInfoPortion::NeedsUpdate(const CStdString &label) const
{
  if (m_label != label)
  {
    m_label = label;
    return true;
  }
  return false;
}

CStdString CGUIInfoLabel::CInfoPortion::Get() const
{
  if (!m_info)
    return m_prefix;
  else if (m_label.empty())
    return "";
  CStdString label = m_prefix + m_label + m_postfix;
  if (m_escaped) // escape all quotes and backslashes, then quote
  {
    label.Replace("\\", "\\\\");
    label.Replace("\"", "\\\"");
    return "\"" + label + "\"";
  }
  return label;
}

CStdString CGUIInfoLabel::GetLabel(const CStdString &label, int contextWindow /*= 0*/, bool preferImage /*= false */)
{ // translate the label
  CGUIInfoLabel info(label, "", contextWindow);
  return info.GetLabel(contextWindow, preferImage);
}

CStdString CGUIInfoLabel::GetItemLabel(const CStdString &label, const CGUIListItem *item, bool preferImage /*= false */)
{ // translate the label
  CGUIInfoLabel info(label);
  return info.GetItemLabel(item, preferImage);
}
