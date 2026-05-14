/*
* Copyright (c) 2013 Rahul Iyer
* All rights reserved.
* 
* Redistribution and use in source and binary forms are permitted provided that
* the above copyright notice and this paragraph are duplicated in all such forms
* and that any documentation, advertising materials, and other materials related
* to such distribution and use acknowledge that the software was developed by 
* Rahul Iyer.  The name of Rahul Iyer may not be used to endorse or promote 
* products derived from this software without specific prior written permission.
* THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF 
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef __DROPBOX_SEARCH_H__
#define __DROPBOX_SEARCH_H__

#include "DropboxMetadataType.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/foreach.hpp>

/**
 * Request for DropboxApi::search().
 *
 * Maps to /2/files/search_v2:
 *   { "query": "...", "options": { "path": "...", "max_results": N,
 *                                  "include_highlights": false } }
 *
 * Note: "include_deleted" is no longer part of v2 search options.
 */
class DropboxSearchRequest {
public:
  DropboxSearchRequest(std::string path,
    std::string query,
    bool include_deleted,
    size_t limit = 100) :
      path_(path),
      query_(query),
      includeDeleted_(include_deleted),
      limit_(limit) {
  }

  std::string getSearchPath()    const { return path_;           }
  std::string getSearchQuery()   const { return query_;          }
  size_t      getResultLimit()   const { return limit_;          }
  bool        shouldIncludeDeleted() const { return includeDeleted_; }

private:
  const std::string   path_;
  const std::string   query_;
  const bool          includeDeleted_;
  const size_t        limit_;
};

/**
 * Result from DropboxApi::search().
 *
 * v2 /2/files/search_v2 response:
 * {
 *   "matches": [
 *     { "metadata": { ".tag": "metadata", "metadata": { <file metadata> } } },
 *     ...
 *   ],
 *   "has_more": false,
 *   "cursor": "..."
 * }
 *
 * Only the first page is returned; pagination is not yet implemented.
 */
class DropboxSearchResult {
public:
  DropboxSearchResult() { }

  static DropboxSearchResult readFromJson(const std::string& json) {
    using namespace std;
    using namespace dropbox;
    using namespace boost::property_tree;
    using namespace boost::property_tree::json_parser;

    stringstream ss;
    ss << json;

    ptree pt;
    read_json(ss, pt);

    vector<DropboxMetadata> v;
    if (pt.count("matches")) {
      BOOST_FOREACH(ptree::value_type& match, pt.get_child("matches")) {
        // matches[].metadata.metadata holds the actual file metadata
        auto metaOpt = match.second.get_child_optional("metadata.metadata");
        if (metaOpt) {
          DropboxMetadata m;
          DropboxMetadata::readFromJson(*metaOpt, m);
          // Only include non-deleted results unless inherit the old
          // "include_deleted" semantics (handled server-side in v2).
          v.push_back(m);
        }
      }
    }

    return DropboxSearchResult(v);
  }

  std::vector<dropbox::DropboxMetadata> const& getResults() const {
    return results_;
  }

private:
  explicit DropboxSearchResult(const std::vector<dropbox::DropboxMetadata>& res) :
    results_(res) {
  }

  std::vector<dropbox::DropboxMetadata> results_;
};

#endif
