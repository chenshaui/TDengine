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
#include "dmInt.h"

static int32_t dmGetMonitorDiskInfo(SDnode *pDnode, SMonDiskInfo *pInfo) {
  tstrncpy(pInfo->logdir.name, tsLogDir, sizeof(pInfo->logdir.name));
  pInfo->logdir.size = tsLogSpace.size;
  tstrncpy(pInfo->tempdir.name, tsTempDir, sizeof(pInfo->tempdir.name));
  pInfo->tempdir.size = tsTempSpace.size;

  SMgmtWrapper *pWrapper = dndAcquireWrapper(pDnode, VNODES);
  if (pWrapper != NULL) {
    vmMonitorTfsInfo(pWrapper, pInfo);
    dndReleaseWrapper(pWrapper);
  }
  return 0;
}

static void dmGetMonitorBasicInfo(SDnode *pDnode, SMonBasicInfo *pInfo) {
  pInfo->protocol = 1;
  pInfo->dnode_id = pDnode->dnodeId;
  pInfo->cluster_id = pDnode->clusterId;
  tstrncpy(pInfo->dnode_ep, tsLocalEp, TSDB_EP_LEN);
}

static void dmGetMonitorDnodeInfo(SDnode *pDnode, SMonDnodeInfo *pInfo) {
  pInfo->uptime = (taosGetTimestampMs() - pDnode->rebootTime) / (86400000.0f);
  taosGetCpuUsage(&pInfo->cpu_engine, &pInfo->cpu_system);
  taosGetCpuCores(&pInfo->cpu_cores);
  taosGetProcMemory(&pInfo->mem_engine);
  taosGetSysMemory(&pInfo->mem_system);
  pInfo->mem_total = tsTotalMemoryKB;
  pInfo->disk_engine = 0;
  pInfo->disk_used = tsDataSpace.size.used;
  pInfo->disk_total = tsDataSpace.size.total;
  taosGetCardInfo(&pInfo->net_in, &pInfo->net_out);
  taosGetProcIO(&pInfo->io_read, &pInfo->io_write, &pInfo->io_read_disk, &pInfo->io_write_disk);

  SMgmtWrapper *pWrapper = dndAcquireWrapper(pDnode, VNODES);
  if (pWrapper != NULL) {
    vmMonitorVnodeReqs(pWrapper, pInfo);
    dndReleaseWrapper(pWrapper);
  }

  pWrapper = dndAcquireWrapper(pDnode, MNODE);
  if (pWrapper != NULL) {
    pInfo->has_mnode = pWrapper->required;
    dndReleaseWrapper(pWrapper);
  }
}

void dmSendMonitorReport(SDnode *pDnode) {
  if (!tsEnableMonitor || tsMonitorFqdn[0] == 0 || tsMonitorPort == 0) return;
  dTrace("send monitor report to %s:%u", tsMonitorFqdn, tsMonitorPort);

  SMonInfo *pMonitor = monCreateMonitorInfo();
  if (pMonitor == NULL) return;

  SMonBasicInfo basicInfo = {0};
  dmGetMonitorBasicInfo(pDnode, &basicInfo);
  monSetBasicInfo(pMonitor, &basicInfo);

  SMonClusterInfo clusterInfo = {0};
  SMonVgroupInfo  vgroupInfo = {0};
  SMonGrantInfo   grantInfo = {0};

  SMgmtWrapper *pWrapper = dndAcquireWrapper(pDnode, MNODE);
  if (pWrapper != NULL) {
    if (mmMonitorMnodeInfo(pWrapper, &clusterInfo, &vgroupInfo, &grantInfo) == 0) {
      monSetClusterInfo(pMonitor, &clusterInfo);
      monSetVgroupInfo(pMonitor, &vgroupInfo);
      monSetGrantInfo(pMonitor, &grantInfo);
    }
    dndReleaseWrapper(pWrapper);
  }

  SMonDnodeInfo dnodeInfo = {0};
  dmGetMonitorDnodeInfo(pDnode, &dnodeInfo);
  monSetDnodeInfo(pMonitor, &dnodeInfo);

  SMonDiskInfo diskInfo = {0};
  if (dmGetMonitorDiskInfo(pDnode, &diskInfo) == 0) {
    monSetDiskInfo(pMonitor, &diskInfo);
  }

  taosArrayDestroy(clusterInfo.dnodes);
  taosArrayDestroy(clusterInfo.mnodes);
  taosArrayDestroy(vgroupInfo.vgroups);
  taosArrayDestroy(diskInfo.datadirs);

  monSendReport(pMonitor);
  monCleanupMonitorInfo(pMonitor);
}

int32_t dmSetDiskInfo(SDnodeMgmt *pMgmt, SNodeMsg *pMsg) {
  SDnode      *pDnode = pMgmt->pDnode;
  SMonDiskInfo info = {0};

  if (tDeserializeSMonDiskInfo(pMsg->rpcMsg.pCont, pMsg->rpcMsg.contLen, &info) != 0) {
    dError("failed to parse diskinfo since %s", terrstr());
    return 0;
  }

  taosWLockLatch(&pMgmt->latch);
  memcpy(&pMgmt->diskInfo, &info, sizeof(SMonDiskInfo));
  taosWUnLockLatch(&pMgmt->latch);

  return 0;
}

int32_t dmSetVnodeStat(SDnodeMgmt *pMgmt, SNodeMsg *pMsg) {
  SDnode      *pDnode = pMgmt->pDnode;
  SVnodesStat info = {0};

  if (tDeserializeSMonDiskInfo(pMsg->rpcMsg.pCont, pMsg->rpcMsg.contLen, &info) != 0) {
    dError("failed to parse diskinfo since %s", terrstr());
    return 0;
  }

  taosWLockLatch(&pMgmt->latch);
  memcpy(&pMgmt->diskInfo, &info, sizeof(SMonDiskInfo));
  taosWUnLockLatch(&pMgmt->latch);

  return 0;
}