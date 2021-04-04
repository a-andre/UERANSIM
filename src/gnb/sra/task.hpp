//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#pragma once

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <memory>
#include <thread>
#include <udp/server_task.hpp>
#include <unordered_map>
#include <urs/rls/gnb_entity.hpp>
#include <urs/sra_pdu.hpp>
#include <utils/logger.hpp>
#include <utils/nts.hpp>
#include <vector>

namespace nr::gnb
{

class GnbSraTask : public NtsTask
{
  private:
    TaskBase *m_base;
    std::unique_ptr<Logger> m_logger;
    udp::UdpServerTask *m_udpTask;

    uint64_t m_sti;
    std::unordered_map<int, std::unique_ptr<SraUeContext>> m_ueCtx;
    std::unordered_map<uint64_t, int> m_stiToUeId;

    friend class GnbCmdHandler;

  public:
    explicit GnbSraTask(TaskBase *base);
    ~GnbSraTask() override = default;

  protected:
    void onStart() override;
    void onLoop() override;
    void onQuit() override;

  private: /* Transport */
    void receiveSraMessage(const InetAddress &addr, const sra::SraMessage &msg);
    void sendSraMessage(const InetAddress &addr, const sra::SraMessage &msg);

  private: /* Handler */
    void handleCellInfoRequest(const InetAddress &addr, const sra::SraCellInfoRequest &msg);

  private: /* UE Management */
    void updateUeInfo(const InetAddress &addr, uint64_t sti);
    void onPeriodicLostControl();
};

} // namespace nr::gnb