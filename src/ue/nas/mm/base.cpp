//
// This file is a part of UERANSIM open source project.
// Copyright (c) 2021 ALİ GÜNGÖR.
//
// The software and all associated files are licensed under GPL-3.0
// and subject to the terms and conditions defined in LICENSE file.
//

#include "mm.hpp"

#include <lib/nas/utils.hpp>
#include <ue/app/task.hpp>
#include <ue/nas/task.hpp>
#include <ue/nas/usim/usim.hpp>
#include <ue/rrc/task.hpp>
#include <utils/common.hpp>

namespace nr::ue
{

static EMmState GetMmStateFromSubState(EMmSubState subState)
{
    switch (subState)
    {
    case EMmSubState::MM_NULL_PS:
        return EMmState::MM_NULL;
    case EMmSubState::MM_DEREGISTERED_PS:
    case EMmSubState::MM_DEREGISTERED_NORMAL_SERVICE:
    case EMmSubState::MM_DEREGISTERED_LIMITED_SERVICE:
    case EMmSubState::MM_DEREGISTERED_ATTEMPTING_REGISTRATION:
    case EMmSubState::MM_DEREGISTERED_PLMN_SEARCH:
    case EMmSubState::MM_DEREGISTERED_NO_SUPI:
    case EMmSubState::MM_DEREGISTERED_NO_CELL_AVAILABLE:
    case EMmSubState::MM_DEREGISTERED_ECALL_INACTIVE:
    case EMmSubState::MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED:
        return EMmState::MM_DEREGISTERED;
    case EMmSubState::MM_REGISTERED_INITIATED_PS:
        return EMmState::MM_REGISTERED_INITIATED;
    case EMmSubState::MM_REGISTERED_PS:
    case EMmSubState::MM_REGISTERED_NORMAL_SERVICE:
    case EMmSubState::MM_REGISTERED_NON_ALLOWED_SERVICE:
    case EMmSubState::MM_REGISTERED_ATTEMPTING_REGISTRATION_UPDATE:
    case EMmSubState::MM_REGISTERED_LIMITED_SERVICE:
    case EMmSubState::MM_REGISTERED_PLMN_SEARCH:
    case EMmSubState::MM_REGISTERED_NO_CELL_AVAILABLE:
    case EMmSubState::MM_REGISTERED_UPDATE_NEEDED:
        return EMmState::MM_REGISTERED;
    case EMmSubState::MM_DEREGISTERED_INITIATED_PS:
        return EMmState::MM_DEREGISTERED_INITIATED;
    case EMmSubState::MM_SERVICE_REQUEST_INITIATED_PS:
        return EMmState::MM_SERVICE_REQUEST_INITIATED;
    }

    std::terminate();
}

NasMm::NasMm(TaskBase *base, NasTimers *timers) : m_base{base}, m_timers{timers}, m_sm{}, m_usim{}
{
    m_logger = base->logBase->makeUniqueLogger(base->config->getLoggerPrefix() + "nas");

    m_rmState = ERmState::RM_DEREGISTERED;
    m_cmState = ECmState::CM_IDLE;
    m_mmState = EMmState::MM_DEREGISTERED;
    m_mmSubState = EMmSubState::MM_DEREGISTERED_PS;

    m_storage = new MmStorage(m_base);
}

void NasMm::onStart(NasSm *sm, Usim *usim)
{
    m_sm = sm;
    m_usim = usim;
    triggerMmCycle();
}

void NasMm::onQuit()
{
    // TODO
}

void NasMm::triggerMmCycle()
{
    m_base->nasTask->push(new NwUeNasToNas(NwUeNasToNas::PERFORM_MM_CYCLE));
}

void NasMm::performMmCycle()
{
    if (m_mmState == EMmState::MM_NULL)
        return;

    if (m_mmState == EMmState::MM_DEREGISTERED)
    {
        if (switchToECallInactivityIfNeeded())
            return;

        if (m_mmSubState == EMmSubState::MM_DEREGISTERED_PS)
        {
            if (m_cmState == ECmState::CM_IDLE)
                switchMmState(EMmSubState::MM_DEREGISTERED_PLMN_SEARCH);
            else
            {
                auto cell = m_base->shCtx.currentCell.get();
                if (cell.hasValue())
                {
                    if (!m_usim->isValid())
                        switchMmState(EMmSubState::MM_DEREGISTERED_NO_SUPI);
                    else if (cell.category == ECellCategory::SUITABLE_CELL)
                        switchMmState(EMmSubState::MM_DEREGISTERED_NORMAL_SERVICE);
                    else if (cell.category == ECellCategory::ACCEPTABLE_CELL)
                        switchMmState(EMmSubState::MM_DEREGISTERED_LIMITED_SERVICE);
                    else
                        switchMmState(EMmSubState::MM_DEREGISTERED_PLMN_SEARCH);
                }
                else
                {
                    switchMmState(EMmSubState::MM_DEREGISTERED_PLMN_SEARCH);
                }
            }
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_NORMAL_SERVICE)
        {
            if (!m_timers->t3346.isRunning())
                sendInitialRegistration(EInitialRegCause::MM_DEREG_NORMAL_SERVICE);
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_LIMITED_SERVICE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_ATTEMPTING_REGISTRATION)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_PLMN_SEARCH)
        {
            performPlmnSelection();
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_NO_SUPI)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_NO_CELL_AVAILABLE)
        {
            performPlmnSelection();
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_ECALL_INACTIVE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_DEREGISTERED_INITIAL_REGISTRATION_NEEDED)
        {
            return;
        }
        return;
    }

    if (m_mmState == EMmState::MM_REGISTERED)
    {
        if (startECallInactivityIfNeeded())
            return;

        if (m_mmSubState == EMmSubState::MM_REGISTERED_PS)
        {
            if (m_cmState == ECmState::CM_IDLE)
                switchMmState(EMmSubState::MM_REGISTERED_PLMN_SEARCH);
            else
            {
                auto cell = m_base->shCtx.currentCell.get();
                if (cell.hasValue())
                {
                    if (cell.category == ECellCategory::SUITABLE_CELL)
                        switchMmState(EMmSubState::MM_REGISTERED_NORMAL_SERVICE);
                    else if (cell.category == ECellCategory::ACCEPTABLE_CELL)
                        switchMmState(EMmSubState::MM_REGISTERED_LIMITED_SERVICE);
                    else
                        switchMmState(EMmSubState::MM_REGISTERED_PLMN_SEARCH);
                }
                else
                {
                    switchMmState(EMmSubState::MM_REGISTERED_PLMN_SEARCH);
                }
            }
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_NORMAL_SERVICE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_NON_ALLOWED_SERVICE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_ATTEMPTING_REGISTRATION_UPDATE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_LIMITED_SERVICE)
        {
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_PLMN_SEARCH)
        {
            performPlmnSelection();
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_NO_CELL_AVAILABLE)
        {
            performPlmnSelection();
            return;
        }
        else if (m_mmSubState == EMmSubState::MM_REGISTERED_UPDATE_NEEDED)
        {
            return;
        }
        return;
    }

    if (m_mmState == EMmState::MM_REGISTERED_INITIATED)
    {
        return;
    }

    if (m_mmState == EMmState::MM_DEREGISTERED_INITIATED)
    {
        return;
    }

    if (m_mmState == EMmState::MM_SERVICE_REQUEST_INITIATED)
    {
        return;
    }
}

void NasMm::switchMmState(EMmSubState subState)
{
    EMmState state = GetMmStateFromSubState(subState);

    ERmState oldRmState = m_rmState;
    if (state == EMmState::MM_DEREGISTERED || state == EMmState::MM_REGISTERED_INITIATED)
        m_rmState = ERmState::RM_DEREGISTERED;
    else if (state == EMmState::MM_REGISTERED || state == EMmState::MM_SERVICE_REQUEST_INITIATED ||
             state == EMmState::MM_DEREGISTERED_INITIATED)
        m_rmState = ERmState::RM_REGISTERED;

    onSwitchRmState(oldRmState, m_rmState);

    if (m_base->nodeListener)
    {
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::RM,
                                       ToJson(oldRmState).str(), ToJson(m_rmState).str());
    }

    EMmState oldState = m_mmState;
    EMmSubState oldSubState = m_mmSubState;

    m_mmState = state;
    m_mmSubState = subState;

    m_lastTimeMmStateChange = utils::CurrentTimeMillis();

    onSwitchMmState(oldState, m_mmState, oldSubState, m_mmSubState);

    if (m_base->nodeListener)
    {
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::MM,
                                       ToJson(oldSubState).str(), ToJson(subState).str());
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::MM_SUB,
                                       ToJson(oldState).str(), ToJson(state).str());
    }

    if (state != oldState || subState != oldSubState)
        m_logger->info("UE switches to state [%s]", ToJson(subState).str().c_str());

    triggerMmCycle();
}

void NasMm::switchCmState(ECmState state)
{
    ECmState oldState = m_cmState;
    m_cmState = state;

    if (state != oldState)
        m_logger->info("UE switches to state [%s]", ToJson(state).str().c_str());

    onSwitchCmState(oldState, m_cmState);

    auto *statusUpdate = new NwUeStatusUpdate(NwUeStatusUpdate::CM_STATE);
    statusUpdate->cmState = m_cmState;
    m_base->appTask->push(statusUpdate);

    if (m_base->nodeListener)
    {
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::CM,
                                       ToJson(oldState).str(), ToJson(m_cmState).str());
    }

    triggerMmCycle();
}

void NasMm::switchUState(E5UState state)
{
    E5UState oldState = m_usim->m_uState;
    m_usim->m_uState = state;

    onSwitchUState(oldState, m_usim->m_uState);

    if (m_base->nodeListener)
    {
        m_base->nodeListener->onSwitch(app::NodeType::UE, m_base->config->getNodeName(), app::StateType::U5,
                                       ToJson(oldState).str(), ToJson(m_usim->m_uState).str());
    }

    if (state != oldState)
        m_logger->info("UE switches to state [%s]", ToJson(state).str().c_str());

    triggerMmCycle();
}

void NasMm::onSwitchMmState(EMmState oldState, EMmState newState, EMmSubState oldSubState, EMmSubState newSubSate)
{
    // The UE shall mark the 5G NAS security context on the USIM or in the non-volatile memory as invalid when the UE
    // initiates an initial registration procedure as described in subclause 5.5.1.2 or when the UE leaves state
    // 5GMM-DEREGISTERED for any other state except 5GMM-NULL.
    if (oldState == EMmState::MM_DEREGISTERED && newState != EMmState::MM_DEREGISTERED && newState != EMmState::MM_NULL)
    {
        if (m_usim->m_currentNsCtx || m_usim->m_nonCurrentNsCtx)
        {
            m_logger->debug("Deleting NAS security context");

            m_usim->m_currentNsCtx = {};
            m_usim->m_nonCurrentNsCtx = {};
        }
    }

    // If the UE enters the 5GMM state 5GMM-DEREGISTERED or 5GMM-NULL,
    // The RAND and RES* values stored in the ME shall be deleted and timer T3516, if running, shall be stopped
    if (newState == EMmState::MM_DEREGISTERED || newState == EMmState::MM_NULL)
    {
        m_usim->m_rand = {};
        m_usim->m_resStar = {};
        m_timers->t3516.stop();
    }

    // If NAS layer starts PLMN SEARCH in CM-CONNECTED, we switch to CM-IDLE. Because PLMN search is an idle
    // operation and RRC expects it in RRC-IDLE state. (This may happen in for example initial registration reject with
    // switch to PLMN search state)
    if (m_cmState == ECmState::CM_CONNECTED && (m_mmSubState == EMmSubState::MM_DEREGISTERED_PLMN_SEARCH ||
                                                m_mmSubState == EMmSubState::MM_REGISTERED_PLMN_SEARCH))
    {
        localReleaseConnection();
    }
}

void NasMm::onSwitchRmState(ERmState oldState, ERmState newState)
{
}

void NasMm::onSwitchCmState(ECmState oldState, ECmState newState)
{
    if (oldState == ECmState::CM_CONNECTED && newState == ECmState::CM_IDLE)
    {
        // 5.5.1.2.7 Abnormal cases in the UE (in registration)
        if (m_mmState == EMmState::MM_REGISTERED_INITIATED)
        {
            // "Lower layer failure or release of the NAS signalling connection received from lower layers before the
            // REGISTRATION ACCEPT or REGISTRATION REJECT message is received. The UE shall abort the registration
            // procedure for initial registration and proceed as ..."

            auto regType = m_lastRegistrationRequest->registrationType.registrationType;

            if (regType == nas::ERegistrationType::INITIAL_REGISTRATION ||
                regType == nas::ERegistrationType::EMERGENCY_REGISTRATION)
            {
                switchMmState(EMmSubState::MM_DEREGISTERED_PS);
                switchUState(E5UState::U2_NOT_UPDATED);

                handleAbnormalInitialRegFailure(regType);
            }
            else
            {
                handleAbnormalMobilityRegFailure(regType);
            }
        }
        // 5.5.2.2.6 Abnormal cases in the UE (in de-registration)
        else if (m_mmState == EMmState::MM_DEREGISTERED_INITIATED)
        {
            // The de-registration procedure shall be aborted and the UE proceeds as follows:
            // if the de-registration procedure was performed due to disabling of 5GS services, the UE shall enter the
            // 5GMM-NULL state;
            if (m_lastDeregCause == EDeregCause::DISABLE_5G)
                switchMmState(EMmSubState::MM_NULL_PS);
            // if the de-registration type "normal de-registration" was requested for reasons other than disabling of
            // 5GS services, the UE shall enter the 5GMM-DEREGISTERED state.
            else if (m_lastDeregistrationRequest->deRegistrationType.switchOff ==
                     nas::ESwitchOff::NORMAL_DE_REGISTRATION)
                switchMmState(EMmSubState::MM_DEREGISTERED_PS);
        }

        // If the UE enters the 5GMM-IDLE, the RAND and RES* values stored
        //  in the ME shall be deleted and timer T3516, if running, shall be stopped
        m_usim->m_rand = {};
        m_usim->m_resStar = {};
        m_timers->t3516.stop();
    }
}

void NasMm::onSwitchUState(E5UState oldState, E5UState newState)
{
}

void NasMm::onSimRemoval()
{
}

void NasMm::onSwitchOff()
{
}

} // namespace nr::ue
