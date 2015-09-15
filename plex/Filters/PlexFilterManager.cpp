#include "PlexFilterManager.h"
#include "PlexDirectory.h"
#include "FileItem.h"
#include "PlexUtils.h"
#include "GUIMessage.h"
#include "PlexTypes.h"
#include "GUIWindowManager.h"
#include "Key.h"
#include "PlexSectionFilter.h"
#include "File.h"
#include "SpecialProtocol.h"
#include "PlexApplication.h"
#include "Client/MyPlex/MyPlexManager.h"
#include "Client/PlexServerDataLoader.h"

#include "XBMCTinyXML.h"

#define PLEX_FILTER_MANAGER_XML_PATH "special://plexprofile/plexfiltermanager2.xml"

///////////////////////////////////////////////////////////////////////////////////////////////////
std::string CPlexFilterManager::getFilterXMLPath()
{
  return PLEX_FILTER_MANAGER_XML_PATH;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexFilterManager::CPlexFilterManager()
{
  loadFiltersFromDisk();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexFilterManager::loadFiltersFromDisk()
{
  CLog::Log(LOGDEBUG, "CPlexFilterManager::loadFiltersFromDisk loading from %s", CSpecialProtocol::TranslatePath(getFilterXMLPath()).c_str());
  if (!XFILE::CFile::Exists(getFilterXMLPath()))
    return;

  CSingleLock lk(m_filterSection);

  CXBMCTinyXML doc;
  doc.LoadFile(getFilterXMLPath());
  if (doc.RootElement())
  {
    m_filtersMap.clear();
    m_myPlexPlaylistFilter = CPlexSectionFilterPtr(new CPlexMyPlexPlaylistFilter(CURL("plexserver://myplex/pms/playlists")));
    m_filtersMap["plexserver://myplex/pms/playlists"] = m_myPlexPlaylistFilter;

    TiXmlElement *root = doc.RootElement();
    TiXmlElement *section = root->FirstChildElement();

    while(section)
    {
      std::string url;
      if (section->QueryStringAttribute("url", &url) == TIXML_SUCCESS)
      {
        CLog::Log(LOGDEBUG, "CPlexFilterManager::loadFiltersFromDisk loading filters for section %s", url.c_str());

        CPlexSectionFilterPtr filter;
        if ("plexserver://myplex/pms/playlists" == url)
          filter = m_myPlexPlaylistFilter;
        else
          filter = CPlexSectionFilterPtr(new CPlexSectionFilter(CURL(url)));

        std::string primaryFilter;
        if(section->QueryStringAttribute("primaryFilter", &primaryFilter) == TIXML_SUCCESS)
          filter->setPrimaryFilter(primaryFilter);

        std::string sortOrder;
        if (section->QueryStringAttribute("sortOrder", &sortOrder) == TIXML_SUCCESS)
          filter->setSortOrder(sortOrder);

        bool sortOrderAsc;
        if (section->QueryBoolAttribute("sortOrderAscending", &sortOrderAsc) == TIXML_SUCCESS)
          filter->setSortOrderAscending(sortOrderAsc);


        TiXmlElement *secondaryFilter = section->FirstChildElement();
        while (secondaryFilter)
        {
          std::string name, key, values, title;
          int type;

          if (secondaryFilter->QueryIntAttribute("type", &type) == TIXML_SUCCESS &&
              secondaryFilter->QueryStringAttribute("name", &name) == TIXML_SUCCESS &&
              secondaryFilter->QueryStringAttribute("values", &values) == TIXML_SUCCESS &&
              secondaryFilter->QueryStringAttribute("key", &key) == TIXML_SUCCESS &&
              secondaryFilter->QueryStringAttribute("title", &title) == TIXML_SUCCESS)
          {
            CLog::Log(LOGDEBUG, "CPlexFilterManager::loadFiltersFromDisk added secondary filter %s %d => %s", title.c_str(), type, values.c_str());
            CPlexSecondaryFilterPtr secFilter = CPlexSecondaryFilterPtr(new CPlexSecondaryFilter(name, key, title, (CPlexSecondaryFilter::SecondaryFilterType)type));
            if (type == CPlexSecondaryFilter::FILTER_TYPE_BOOLEAN)
              secFilter->setSelected(true);
            else
              secFilter->setSelected(values);
            filter->addSecondaryFilter(secFilter);
          }

          secondaryFilter = secondaryFilter->NextSiblingElement();
        }

        m_filtersMap[url] = filter;
      }
      section = section->NextSiblingElement();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexFilterManager::saveFiltersToDisk()
{
  CSingleLock lk(m_filterSection);
  CXBMCTinyXML doc;

  TiXmlDeclaration decl("1.0", "utf-8", "");
  doc.InsertEndChild(decl);

  TiXmlElement root("FilterManager");

  std::pair<std::string, CPlexSectionFilterPtr> p;
  BOOST_FOREACH(p, m_filtersMap)
  {
    TiXmlElement section("Section");
    section.SetAttribute("url", p.first);
    section.SetAttribute("primaryFilter", p.second->currentPrimaryFilter());
    section.SetAttribute("sortOrder", p.second->currentSortOrder());
    section.SetAttribute("sortOrderAscending", p.second->currentSortOrderAscending());

    std::vector<CPlexSecondaryFilterPtr> secFilter = p.second->currentSecondaryFilters();
    BOOST_FOREACH(CPlexSecondaryFilterPtr filter, secFilter)
    {
      TiXmlElement filterEl("SecondaryFilter");
      std::pair<std::string, std::string> fp = filter->getFilterKeyValue();
      filterEl.SetAttribute("key", filter->getFilterKey());
      filterEl.SetAttribute("values", fp.second);
      filterEl.SetAttribute("type", filter->getFilterType());
      filterEl.SetAttribute("name", filter->getFilterName());
      filterEl.SetAttribute("title", filter->getFilterTitle());
      section.InsertEndChild(filterEl);
    }

    root.InsertEndChild(section);
  }

  doc.InsertEndChild(root);
  doc.SaveFile(getFilterXMLPath());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexSectionFilterPtr CPlexFilterManager::getFilterForSection(const std::string &sectionUrl)
{
  if (m_myPlexPlaylistFilter && sectionUrl == m_myPlexPlaylistFilter->getFilterUrl())
    return m_myPlexPlaylistFilter;

  CSingleLock lk(m_filterSection);
  if (m_filtersMap.find(sectionUrl) != m_filtersMap.end())
    return m_filtersMap[sectionUrl];

  return CPlexSectionFilterPtr();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CPlexSectionFilterPtr CPlexFilterManager::loadFilterForSection(const std::string &sectionUrl, bool forceReload)
{
  CSingleLock lk(m_filterSection);

  CPlexSectionFilterPtr filter = getFilterForSection(sectionUrl);
  if (filter)
  {
    if (filter->isLoaded() && !forceReload)
      onFilterLoaded(filter->getFilterUrl());
    else
      CJobManager::GetInstance().AddJob(new CPlexSectionFilterLoadJob(filter), this, CJob::PRIORITY_LOW);
    return filter;
  }

  CURL url(sectionUrl);
  CFileItemPtr section = g_plexApplication.dataLoader->GetSection(url);
  if (section)
  {
    filter = CPlexSectionFilterPtr(new CPlexSectionFilter(url));
    m_filtersMap[filter->getFilterUrl()] = filter;
    CJobManager::GetInstance().AddJob(new CPlexSectionFilterLoadJob(filter), this, CJob::PRIORITY_LOW);
    return filter;
  }

  return CPlexSectionFilterPtr();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexFilterManager::onFilterLoaded(const std::string &sectionUrl)
{
  CGUIMessage msg(GUI_MSG_FILTER_LOADED, 0, 0);
  msg.SetStringParam(sectionUrl);
  g_windowManager.SendThreadMessage(msg, g_windowManager.GetActiveWindow());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CPlexFilterManager::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CPlexSectionFilterLoadJob *ljob = static_cast<CPlexSectionFilterLoadJob*>(job);
  if (ljob && success)
  {
    CPlexSectionFilterPtr filter = ljob->m_sectionFilter;
    onFilterLoaded(filter->getFilterUrl());
  }
}

