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

#ifndef __DROPBOX_ACCOUNT_INFO_H__
#define __DROPBOX_ACCOUNT_INFO_H__

#include <string>
#include <sys/types.h>

namespace dropbox {

/**
 * Space usage as returned by /2/users/get_space_usage.
 *
 * v1 → v2 field mapping:
 *   quota  → allocation.allocated (total bytes granted)
 *   normal → used                 (bytes used by owned files)
 *   shared → 0                    (no longer reported separately)
 */
struct DropboxQuotaInfo {
  uint64_t    used      = 0;  ///< Total bytes used across the account
  uint64_t    allocated = 0;  ///< Total bytes allocated (quota)
};

/**
 * Account information as returned by /2/users/get_current_account.
 *
 * Changes from v1:
 *   - getUid()          returns account_id (e.g. "dbid:AAH4f…")
 *   - getDisplayName()  returns name.display_name
 *   - getReferralLink() returns "" (field removed in v2)
 *   - getQuotaInfo()    is populated by a second call to
 *                       /2/users/get_space_usage inside DropboxApi::getAccountInfo()
 */
class DropboxAccountInfo {
public:
  DropboxAccountInfo() { }
  DropboxAccountInfo(std::string& json);

  void            readJson(std::string& json);
  void            readSpaceUsageJson(std::string& json);

  std::string         getReferralLink() const;  ///< Always "" in v2
  std::string         getDisplayName() const;
  std::string         getUid() const;           ///< Returns account_id
  std::string         getCountry() const;
  std::string         getEmail() const;
  DropboxQuotaInfo    getQuotaInfo() const;

private:
  static void readFromJson(DropboxAccountInfo*, std::string& json);
  static void readSpaceUsageFromJson(DropboxAccountInfo*, std::string& json);

  std::string           displayName_;
  std::string           uid_;         // stores account_id from v2
  std::string           country_;
  std::string           email_;
  DropboxQuotaInfo      quotaInfo_;
};

} // namespace dropbox
#endif
