#include "global.h"
#include "ArchHooks_darwin.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "RageTimer.h"
#include "archutils/Darwin/Crash.h"
#include "archutils/Unix/CrashHandler.h"
#include "archutils/Unix/SignalHandler.h"
#include "StepMania.h"
#define Random Random_ // work around namespace pollution
#include <Carbon/Carbon.h>
#undef Random_
#include <mach/thread_act.h>
#include <sys/types.h>
#include <sys/sysctl.h>

/* You would think that these would be defined somewhere. */
enum
{
    kMacOSX_10_2 = 0x1020,
    kMacOSX_10_3 = 0x1030
};

static thread_time_constraint_policy g_oldttcpolicy;
static float g_fStartedTimeCritAt;

SInt16 ShowAlert(int type, CFStringRef message, CFStringRef OK, CFStringRef cancel = NULL)
{
    struct AlertStdCFStringAlertParamRec params = {kStdCFStringAlertVersionOne, true, false, OK, cancel, NULL,
        kAlertStdAlertOKButton, kAlertStdAlertCancelButton, kWindowAlertPositionParentWindowScreen, NULL};
    DialogRef dialog;
    SInt16 result;
    OSErr err;

    CreateStandardAlert(type, message, NULL, &params, &dialog);
    err = AutoSizeDialog(dialog);
    ASSERT(err == noErr);
    RunStandardAlert(dialog, NULL, &result);

    return result;
}

static bool IsFatalSignal( int signal )
{
	switch( signal )
	{
	case SIGINT:
	case SIGTERM:
	case SIGHUP:
		return false;
	default:
		return true;
	}
}

static void DoCleanShutdown( int signal, siginfo_t *si, const ucontext_t *uc )
{
	if( IsFatalSignal(signal) )
		return;

	/* ^C. */
	ExitGame();
}

static void DoCrashSignalHandler( int signal, siginfo_t *si, const ucontext_t *uc )
{
	/* Don't dump a debug file if the user just hit ^C. */
	if( !IsFatalSignal(signal) )
		return;

	CrashSignalHandler( signal, si, uc );
	/* not reached */
}

ArchHooks_darwin::ArchHooks_darwin()
{
    CrashHandlerHandleArgs(g_argc, g_argv);

    /* First, handle non-fatal termination signals. */
    SignalHandler::OnClose( DoCleanShutdown );

    SignalHandler::OnClose( DoCrashSignalHandler );
    TimeCritMutex = new RageMutex("TimeCritMutex");
}

ArchHooks_darwin::~ArchHooks_darwin()
{
	delete TimeCritMutex;
}

#define CASE_GESTALT_M(str,code,result) case gestalt##code: str = result; break
#define CASE_GESTALT(str,code) CASE_GESTALT_M(str, code, #code)

void ArchHooks_darwin::DumpDebugInfo()
{
    CString systemVersion;
    long ram;
    long vRam;
    long processorSpeed;
    CString processor;
    long numProcessors;
    CString machine;
    char *temp;

    OSErr err = noErr;
    long code;

    /* Get system version */
    err = Gestalt(gestaltSystemVersion, &code);
    if (err == noErr)
    {
        systemVersion = "Mac OS X ";
        if (code >= kMacOSX_10_2 && code < kMacOSX_10_3)
        {
            systemVersion += "10.2.";
            asprintf(&temp, "%d", code - kMacOSX_10_2);
            systemVersion += temp;
            free(temp);
        }
        else
        {
            systemVersion += "10.3";
            int ssv = code - kMacOSX_10_3;
            if (ssv > 9)
                systemVersion += "+";
            else {
                asprintf(&temp, ".%d", ssv);
                systemVersion += temp;
                free(temp);
            }
        }
    }
    else
        systemVersion = "Unknown system version";
            
    /* Get memory */
    err = Gestalt(gestaltLogicalRAMSize, &vRam);
    if (err != noErr)
        vRam = 0;
    err = Gestalt(gestaltPhysicalRAMSize, &ram);
    if (err == noErr)
    {
        vRam -= ram;
        if (vRam < 0)
            vRam = 0;
        ram /= 1048576; /* 1048576 = 1024*1024 */
        vRam /= 1048576;
    }
    else
    {
        ram = 0;
        vRam = 0;
    }
    
    /* XXX update this information for G5s */
    /* Get processor */
    numProcessors = MPProcessorsScheduled();
    err = Gestalt(gestaltNativeCPUtype, &code);
    if (err == noErr)
    {
        switch (code)
        {
            CASE_GESTALT_M(processor, CPU601, "601");
            CASE_GESTALT_M(processor, CPU603, "603");
            CASE_GESTALT_M(processor, CPU603e, "603e");
            CASE_GESTALT_M(processor, CPU603ev, "603ev");
            CASE_GESTALT_M(processor, CPU604, "604");
            CASE_GESTALT_M(processor, CPU604e, "604e");
            CASE_GESTALT_M(processor, CPU604ev, "604ev");
            CASE_GESTALT_M(processor, CPU750, "G3");
            CASE_GESTALT_M(processor, CPUG4, "G4");
            CASE_GESTALT_M(processor, CPUG47450, "G4");
            CASE_GESTALT_M(processor, CPUApollo, "G4 (Apollo)");
            CASE_GESTALT_M(processor, CPU750FX, "G3 (Sahara)");
            default:
                asprintf(&temp, "%d", code);
                processor = temp;
                free(temp);
        }
    }
    else
        processor = "unknown";
    err = Gestalt(gestaltProcClkSpeed, &processorSpeed);
    if (err != noErr)
        processorSpeed = 0;
    /* Get machine */
    err = Gestalt(gestaltMachineType, &code);
    if (err == noErr)
    {
        switch (code)
        {
            /* PowerMacs */
            CASE_GESTALT(machine, PowerMac4400);
            CASE_GESTALT(machine, PowerMac4400_160);
            CASE_GESTALT(machine, PowerMac5200);
            CASE_GESTALT(machine, PowerMac5400);
            CASE_GESTALT(machine, PowerMac5500);
            CASE_GESTALT(machine, PowerMac6100_60);
            CASE_GESTALT(machine, PowerMac6100_66);
            CASE_GESTALT(machine, PowerMac6200);
            CASE_GESTALT(machine, PowerMac6400);
            CASE_GESTALT(machine, PowerMac6500);
            CASE_GESTALT(machine, PowerMac7100_66);
            CASE_GESTALT(machine, PowerMac7100_80);
            CASE_GESTALT(machine, PowerMac7200);
            CASE_GESTALT(machine, PowerMac7300);
            CASE_GESTALT(machine, PowerMac7500);
            CASE_GESTALT(machine, PowerMac8100_80);
            CASE_GESTALT(machine, PowerMac8100_100);
            CASE_GESTALT(machine, PowerMac8100_110);
            CASE_GESTALT(machine, PowerMac8500);
            CASE_GESTALT(machine, PowerMac9500);
            /* upgrade cards */
            CASE_GESTALT(machine, PowerMacLC475);
            CASE_GESTALT(machine, PowerMacLC575);
            CASE_GESTALT(machine, PowerMacQuadra610);
            CASE_GESTALT(machine, PowerMacQuadra630);
            CASE_GESTALT(machine, PowerMacQuadra650);
            CASE_GESTALT(machine, PowerMacQuadra700);
            CASE_GESTALT(machine, PowerMacQuadra800);
            CASE_GESTALT(machine, PowerMacQuadra900);
            CASE_GESTALT(machine, PowerMacQuadra950);
            CASE_GESTALT(machine, PowerMacCentris610);
            CASE_GESTALT(machine, PowerMacCentris650);
            /* PowerBooks */
            CASE_GESTALT(machine, PowerBook1400);
            CASE_GESTALT(machine, PowerBook2400);
            CASE_GESTALT(machine, PowerBook3400);
            CASE_GESTALT(machine, PowerBook500PPCUpgrade);
            CASE_GESTALT(machine, PowerBookG3);
            CASE_GESTALT(machine, PowerBookG3Series);
            CASE_GESTALT(machine, PowerBookG3Series2);
            /* NewWorld */
            CASE_GESTALT(machine, PowerMacNewWorld);
            CASE_GESTALT(machine, PowerMacG3);
            default:
                asprintf(&temp, "%d", code);
                machine = temp;
                free(temp);
        }
    }
    else if (err == gestaltUndefSelectorErr ) {
        machine = "PowerMac";
        machine += processor;
    }
    else
        machine = "unknown machine";
    
    /* Send all of the information to the log */
    LOG->Info(machine.c_str());
    LOG->Info("Processor: %s (%ld)", processor.c_str(), numProcessors);
    LOG->Info("%s", systemVersion.c_str());
    LOG->Info("Memory: %ld MB total, %ld MB swap", ram, vRam);
}

void ArchHooks_darwin::MessageBoxOKPrivate(CString sMessage, CString ID)
{
    bool allowHush = ID != "";
    
    if (allowHush && MessageIsIgnored(ID))
        return;

    CFStringRef message = CFStringCreateWithCString(NULL, sMessage, kCFStringEncodingASCII);
    SInt16 result = ShowAlert(kAlertNoteAlert, message, CFSTR("OK"), CFSTR("Don't show again"));

    CFRelease(message);
    if (result == kAlertStdAlertCancelButton && allowHush)
        IgnoreMessage(ID);
}

void ArchHooks_darwin::MessageBoxErrorPrivate(CString sError, CString ID)
{
    CFStringRef error = CFStringCreateWithCString(NULL, sError, kCFStringEncodingASCII);
    ShowAlert(kAlertStopAlert, error, CFSTR("OK"));

    CFRelease(error);
}

ArchHooks::MessageBoxResult ArchHooks_darwin::MessageBoxAbortRetryIgnorePrivate(CString sMessage, CString ID)
{
    CFStringRef error = CFStringCreateWithCString(NULL, sMessage, kCFStringEncodingASCII);
    SInt16 result = ShowAlert(kAlertNoteAlert, error, CFSTR("Retry"), CFSTR("Ignore"));
    ArchHooks::MessageBoxResult ret;

    CFRelease(error);
    switch (result)
    {
        case kAlertStdAlertOKButton:
            ret = retry;
            break;
        case kAlertStdAlertCancelButton:
            ret = ignore;
            break;
        default:
            ASSERT(0);
            ret = ignore;
    }
    
    return ret;
}

void ArchHooks_darwin::EnterTimeCriticalSection()
{
	TimeCritMutex->Lock();

	int mib[] = { CTL_HW, HW_BUS_FREQ };
	int miblen = ARRAYSIZE( mib );
	int bus_speed;
	size_t len = sizeof (bus_speed);
	if( sysctl( mib, miblen, &bus_speed, &len, NULL, 0 ) == -1 )
	{
		LOG->Warn( "sysctl(HW_BUS_FREQ): %s", strerror(errno) );
		return;
	}

	mach_msg_type_number_t cnt = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
	boolean_t bDefaults = false;
	thread_policy_get( mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (int*)&g_oldttcpolicy, &cnt, &bDefaults );

	/* We want to monopolize the CPU for a very short period of time.  This means that the
	 * period doesn't really matter, and we don't want to be preempted.  Set the period
	 * very high (~1 second), so that if we ever lose the CPU when we shouldn't, we can
	 * detect it and log it in ExitTimeCriticalSection(). */
	thread_time_constraint_policy ttcpolicy;
	ttcpolicy.period = bus_speed;
	ttcpolicy.computation = ttcpolicy.constraint = bus_speed/60;
	ttcpolicy.preemptible = 0;
	thread_policy_set( mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY,
		(int*)&ttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT );

	g_fStartedTimeCritAt = RageTimer::GetTimeSinceStart();
}

void ArchHooks_darwin::ExitTimeCriticalSection()
{
	thread_policy_set( mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY,
		(int*) &g_oldttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT );
	TimeCritMutex->Unlock();

	float fTimeCritLen = RageTimer::GetTimeSinceStart() - g_fStartedTimeCritAt;
	if( fTimeCritLen > 0.1f )
		LOG->Warn( "Time-critical section lasted for %f", fTimeCritLen );
}

/*
 * (c) 2003-2004 Steve Checkoway
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
