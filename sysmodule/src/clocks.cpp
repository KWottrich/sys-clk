/*
 * --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy us a beer in return.  - The sys-clk authors
 * --------------------------------------------------------------------------
 */

#include <nxExt.h>
#include "clocks.h"
#include "errors.h"

void Clocks::GetList(SysClkModule module, std::uint32_t **outClocks)
{
    switch(module)
    {
        case SysClkModule_CPU:
            *outClocks = sysclk_g_freq_table_cpu_hz;
            break;
        case SysClkModule_GPU:
            *outClocks = sysclk_g_freq_table_gpu_hz;
            break;
        case SysClkModule_MEM:
            *outClocks = sysclk_g_freq_table_mem_hz;
            break;
        default:
            *outClocks = NULL;
            ERROR_THROW("No such PcvModule: %u", module);
    }
}

void Clocks::Initialize()
{
    Result rc = 0;

    if(hosversionAtLeast(8,0,0))
    {
        rc = clkrstInitialize();
        ASSERT_RESULT_OK(rc, "pcvInitialize");
    }
    else
    {
        rc = pcvInitialize();
        ASSERT_RESULT_OK(rc, "pcvInitialize");
    }

    rc = apmExtInitialize();
    ASSERT_RESULT_OK(rc, "apmExtInitialize");

    rc = psmInitialize();
    ASSERT_RESULT_OK(rc, "psmInitialize");
}

void Clocks::Exit()
{
    if(hosversionAtLeast(8,0,0))
    {
        pcvExit();
    }
    else
    {
        clkrstExit();
    }
    apmExtExit();
    psmExit();
}

const char* Clocks::GetModuleName(SysClkModule module, bool pretty)
{
    const char* result = SysClkFormatModule(module, pretty);

    if(!result)
    {
        ERROR_THROW("No such SysClkModule: %u", module);
    }

    return result;
}

const char* Clocks::GetProfileName(SysClkProfile profile, bool pretty)
{
    const char* result = SysClkFormatProfile(profile, pretty);

    if(!result)
    {
        ERROR_THROW("No such SysClkProfile: %u", profile);
    }

    return result;
}

PcvModule Clocks::GetPcvModule(SysClkModule SysClkModule)
{
    switch(SysClkModule)
    {
        case SysClkModule_CPU:
            return PcvModule_CpuBus;
        case SysClkModule_GPU:
            return PcvModule_GPU;
        case SysClkModule_MEM:
            return PcvModule_EMC;
        default:
            ERROR_THROW("No such SysClkModule: %u", SysClkModule);
    }

    return (PcvModule)0;
}

PcvModuleId Clocks::GetPcvModuleId(SysClkModule SysClkModule)
{
    PcvModuleId pcvModuleId;
    Result rc = pcvGetModuleId(&pcvModuleId, GetPcvModule(SysClkModule));
    ASSERT_RESULT_OK(rc, "pcvGetModuleId");

    return pcvModuleId;
}

std::uint32_t Clocks::ResetToStock()
{
    std::uint32_t mode = 0;
    Result rc = apmExtGetPerformanceMode(&mode);
    ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

    rc = apmExtSysRequestPerformanceMode(mode);
    ASSERT_RESULT_OK(rc, "apmExtSysRequestPerformanceMode");

    return mode;
}

SysClkProfile Clocks::GetCurrentProfile()
{
    std::uint32_t mode = 0;
    Result rc = apmExtGetPerformanceMode(&mode);
    ASSERT_RESULT_OK(rc, "apmExtGetPerformanceMode");

    if(mode)
    {
        return SysClkProfile_Docked;
    }

    ChargerType chargerType;

    rc = psmGetChargerType(&chargerType);
    ASSERT_RESULT_OK(rc, "psmGetChargerType");

    if(chargerType == ChargerType_Charger)
    {
        return SysClkProfile_HandheldChargingOfficial;
    }
    else if(chargerType == ChargerType_Usb)
    {
        return SysClkProfile_HandheldChargingUSB;
    }

    return SysClkProfile_Handheld;
}

void Clocks::SetHz(SysClkModule module, std::uint32_t hz)
{
    Result rc = 0;

    if(hosversionAtLeast(8,0,0))
    {
        ClkrstSession session = {0};

        rc = clkrstOpenSession(&session, Clocks::GetPcvModuleId(module), 3);
        ASSERT_RESULT_OK(rc, "clkrstOpenSession");

        rc = clkrstSetClockRate(&session, hz);
        ASSERT_RESULT_OK(rc, "clkrstSetClockRate");

        clkrstCloseSession(&session);
    }
    else
    {
        rc = pcvSetClockRate(Clocks::GetPcvModule(module), hz);
        ASSERT_RESULT_OK(rc, "pcvSetClockRate");
    }
}

std::uint32_t Clocks::GetCurrentHz(SysClkModule module)
{
    Result rc = 0;
    std::uint32_t hz = 0;

    if(hosversionAtLeast(8,0,0))
    {
        ClkrstSession session = {0};

        rc = clkrstOpenSession(&session, Clocks::GetPcvModuleId(module), 3);
        ASSERT_RESULT_OK(rc, "clkrstOpenSession");

        rc = clkrstGetClockRate(&session, &hz);
        ASSERT_RESULT_OK(rc, "clkrstSetClockRate");

        clkrstCloseSession(&session);
    }
    else
    {
        rc = pcvGetClockRate(Clocks::GetPcvModule(module), &hz);
        ASSERT_RESULT_OK(rc, "pcvGetClockRate");
    }

    return hz;
}

std::uint32_t Clocks::GetNearestHz(SysClkModule module, SysClkProfile profile, std::uint32_t inHz)
{
    std::uint32_t hz = GetNearestHz(module, inHz);
    std::uint32_t maxHz = GetMaxAllowedHz(module, profile);

    if(maxHz != 0)
    {
        hz = std::min(hz, maxHz);
    }

    return hz;
}

std::uint32_t Clocks::GetMaxAllowedHz(SysClkModule module, SysClkProfile profile)
{
    if(module == SysClkModule_GPU)
    {
        if(profile < SysClkProfile_HandheldCharging)
        {
            return SYSCLK_GPU_HANDHELD_MAX_HZ;
        }
        else if(profile <= SysClkProfile_HandheldChargingUSB)
        {
            return SYSCLK_GPU_UNOFFICIAL_CHARGER_MAX_HZ;
        }
    }

    return 0;
}

std::uint32_t Clocks::GetNearestHz(SysClkModule module, std::uint32_t inHz)
{
    std::uint32_t *clockTable = NULL;
    GetList(module, &clockTable);

    if (!clockTable || !clockTable[0])
    {
        ERROR_THROW("table lookup failed for SysClkModule: %u", module);
    }

    int i = 0;
    while(clockTable[i + 1])
    {
        if (inHz <= (clockTable[i] + clockTable[i + 1]) / 2)
        {
            break;
        }
        i++;
    }

    return clockTable[i];
}