/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "taoserror.h"
#include "tref.h"
#include "walInt.h"

int walInitWriteFile(SWal* pWal) {
  TdFilePtr     pIdxTFile, pLogTFile;
  SWalFileInfo* pRet = taosArrayGetLast(pWal->fileInfoSet);
  int64_t       fileFirstVer = pRet->firstVer;

  char fnameStr[WAL_FILE_LEN];
  walBuildIdxName(pWal, fileFirstVer, fnameStr);
  pIdxTFile = taosOpenFile(fnameStr, TD_FILE_CREATE | TD_FILE_WRITE | TD_FILE_APPEND);
  if (pIdxTFile == NULL) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    return -1;
  }
  walBuildLogName(pWal, fileFirstVer, fnameStr);
  pLogTFile = taosOpenFile(fnameStr, TD_FILE_CREATE | TD_FILE_WRITE | TD_FILE_APPEND);
  if (pLogTFile == NULL) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    return -1;
  }
  // switch file
  pWal->pIdxFile = pIdxTFile;
  pWal->pLogFile = pLogTFile;
  pWal->writeCur = taosArrayGetSize(pWal->fileInfoSet) - 1;
  return 0;
}

int64_t walChangeWrite(SWal* pWal, int64_t ver) {
  int       code;
  TdFilePtr pIdxTFile, pLogTFile;
  char      fnameStr[WAL_FILE_LEN];
  if (pWal->pLogFile != NULL) {
    if (pWal->cfg.level != TAOS_WAL_SKIP && (code = taosFsyncFile(pWal->pLogFile)) != 0) {
      terrno = TAOS_SYSTEM_ERROR(errno);
      return -1;
    }
    code = taosCloseFile(&pWal->pLogFile);
    if (code != 0) {
      terrno = TAOS_SYSTEM_ERROR(errno);
      return -1;
    }
  }
  if (pWal->pIdxFile != NULL) {
    if (pWal->cfg.level != TAOS_WAL_SKIP && (code = taosFsyncFile(pWal->pIdxFile)) != 0) {
      terrno = TAOS_SYSTEM_ERROR(errno);
      return -1;
    }
    code = taosCloseFile(&pWal->pIdxFile);
    if (code != 0) {
      terrno = TAOS_SYSTEM_ERROR(errno);
      return -1;
    }
  }

  SWalFileInfo tmpInfo;
  tmpInfo.firstVer = ver;
  // bsearch in fileSet
  int32_t idx = taosArraySearchIdx(pWal->fileInfoSet, &tmpInfo, compareWalFileInfo, TD_LE);
  /*A(idx != -1);*/
  SWalFileInfo* pFileInfo = taosArrayGet(pWal->fileInfoSet, idx);

  int64_t fileFirstVer = pFileInfo->firstVer;
  walBuildIdxName(pWal, fileFirstVer, fnameStr);
  pIdxTFile = taosOpenFile(fnameStr, TD_FILE_CREATE | TD_FILE_WRITE | TD_FILE_APPEND);
  if (pIdxTFile == NULL) {
    terrno = TAOS_SYSTEM_ERROR(errno);
    pWal->pIdxFile = NULL;
    return -1;
  }
  walBuildLogName(pWal, fileFirstVer, fnameStr);
  pLogTFile = taosOpenFile(fnameStr, TD_FILE_CREATE | TD_FILE_WRITE | TD_FILE_APPEND);
  if (pLogTFile == NULL) {
    taosCloseFile(&pIdxTFile);
    terrno = TAOS_SYSTEM_ERROR(errno);
    pWal->pLogFile = NULL;
    return -1;
  }

  pWal->pLogFile = pLogTFile;
  pWal->pIdxFile = pIdxTFile;
  pWal->writeCur = idx;
  return fileFirstVer;
}
