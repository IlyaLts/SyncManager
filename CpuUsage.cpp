/*
===============================================================================
    Copyright (C) 2022-2025 Ilya Lyakhovets

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
===============================================================================
*/

#include "CpuUsage.h"
#include <processthreadsapi.h>

/*
===================
CpuUsage::CpuUsage
===================
*/
CpuUsage::CpuUsage(QObject *parent) : QObject(parent)
{
    process = GetCurrentProcess();
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &CpuUsage::updateCpuUsage);

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    processors = sysInfo.dwNumberOfProcessors;

    if (!processors)
        processors = 1;
}

/*
===================
CpuUsage::~CpuUsage
===================
*/
CpuUsage::~CpuUsage()
{
    if (process)
        CloseHandle(process);
}

/*
===================
CpuUsage::startMonitoring
===================
*/
void CpuUsage::startMonitoring(int interval)
{
    this->interval = interval;
    GetProcessTimes(lastProcessKernelTime, lastProcessUserTime);
    GetSystemTimes(lastSystemIdleTime, lastSystemKernelTime, lastSystemUserTime);
    timer->start(interval);
}

/*
===================
CpuUsage::stopMonitoring
===================
*/
void CpuUsage::stopMonitoring()
{
    timer->stop();
}

/*
===================
CpuUsage::updateCpuUsage
===================
*/
void CpuUsage::updateCpuUsage()
{
    ULONGLONG currentProcessKernelTime;
    ULONGLONG currentProcessUserTime;
    ULONGLONG currentSystemIdleTime;
    ULONGLONG currentSystemKernelTime;
    ULONGLONG currentSystemUserTime;

    if (!GetProcessTimes(currentProcessKernelTime, currentProcessUserTime))
    {
        emit cpuUsageUpdated(0.0, 0.0);
        return;
    }

    if (!GetSystemTimes(currentSystemIdleTime, currentSystemKernelTime, currentSystemUserTime))
    {
        emit cpuUsageUpdated(0.0, 0.0);
        return;
    }

    // Application CPU usage
    ULONGLONG processKernelTimeDelta = currentProcessKernelTime - lastProcessKernelTime;
    ULONGLONG processUserTimeDelta = currentProcessUserTime - lastProcessUserTime;
    ULONGLONG processTotalTimeDelta = processKernelTimeDelta + processUserTimeDelta;

    // System CPU usage
    ULONGLONG systemIdleTimeDelta = currentSystemIdleTime - lastSystemIdleTime;
    ULONGLONG systemKernelTimeDelta = currentSystemKernelTime - lastSystemKernelTime;
    ULONGLONG systemUserTimeDelta = currentSystemUserTime - lastSystemUserTime;
    ULONGLONG systemTotalTimeDelta = systemKernelTimeDelta + systemUserTimeDelta;
    ULONGLONG totalSystemCpuTimeDelta = systemTotalTimeDelta + systemIdleTimeDelta;

    float appCpuPercentage = 0.0;
    float systemCpuPercentage = 0.0;

    if (totalSystemCpuTimeDelta > 0/* && processors > 0*/)
        appCpuPercentage = (static_cast<float>(processTotalTimeDelta) / totalSystemCpuTimeDelta) * 100.0;// / processors;

    if (totalSystemCpuTimeDelta > 0)
        systemCpuPercentage = (static_cast<float>(systemTotalTimeDelta) / totalSystemCpuTimeDelta) * 100.0;

    emit cpuUsageUpdated(appCpuPercentage, systemCpuPercentage);

    lastProcessKernelTime = currentProcessKernelTime;
    lastProcessUserTime = currentProcessUserTime;
    lastSystemIdleTime = currentSystemIdleTime;
    lastSystemKernelTime = currentSystemKernelTime;
    lastSystemUserTime = currentSystemUserTime;
}

/*
===================
CpuUsage::GetProcessTimes
===================
*/
bool CpuUsage::GetProcessTimes(ULONGLONG &kernelTime, ULONGLONG &userTime)
{
    FILETIME creationFileTime;
    FILETIME exitFileTime;
    FILETIME kernelFileTime;
    FILETIME userFileTime;

    if (::GetProcessTimes(process, &creationFileTime, &exitFileTime, &kernelFileTime, &userFileTime))
    {
        kernelTime = FileTimeToULONGLONG(kernelFileTime);
        userTime = FileTimeToULONGLONG(userFileTime);

        return true;
    }

    return false;
}

/*
===================
CpuUsage::GetSystemTimes
===================
*/
bool CpuUsage::GetSystemTimes(ULONGLONG &idleTime, ULONGLONG &kernelTime, ULONGLONG &userTime)
{
    FILETIME idleFiletime;
    FILETIME kernelFileTime;
    FILETIME userFileTime;

    if (::GetSystemTimes(&idleFiletime, &kernelFileTime, &userFileTime))
    {
        idleTime = FileTimeToULONGLONG(idleFiletime);
        kernelTime = FileTimeToULONGLONG(kernelFileTime);
        userTime = FileTimeToULONGLONG(userFileTime);

        return true;
    }

    return false;
}

/*
===================
CpuUsage::FileTimeToULONGLONG
===================
*/
ULONGLONG CpuUsage::FileTimeToULONGLONG(const FILETIME &ft)
{
    // FILETIME contains a 64-bit value representing the number of 100-nanosecond intervals since January 1, 1601 (UTC).
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
