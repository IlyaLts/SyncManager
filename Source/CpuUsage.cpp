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

#ifdef Q_OS_WIN
#include <processthreadsapi.h>
#else
#include <QFile>
#include <QCoreApplication>
#endif

/*
===================
CpuUsage::CpuUsage
===================
*/
CpuUsage::CpuUsage(QObject *parent) : QObject(parent)
{
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &CpuUsage::updateCpuUsage);

#ifdef Q_OS_WIN
    process = GetCurrentProcess();

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    processors = sysInfo.dwNumberOfProcessors;

    if (!processors)
        processors = 1;
#else
    m_numCores = sysconf(_SC_NPROCESSORS_ONLN);
    m_pid = QCoreApplication::applicationPid();
#endif
}

/*
===================
CpuUsage::~CpuUsage
===================
*/
CpuUsage::~CpuUsage()
{
#ifdef Q_OS_WIN
    if (process)
        CloseHandle(process);
#endif
}

/*
===================
CpuUsage::startMonitoring
===================
*/
void CpuUsage::startMonitoring(int interval)
{
    this->interval = interval;
    timer->start(interval);

    GetProcessTimes(lastProcessKernelTime, lastProcessUserTime);
    GetSystemTimes(lastSystemIdleTime, lastSystemKernelTime, lastSystemUserTime);
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
    cpuTime_t currentProcessKernelTime;
    cpuTime_t currentProcessUserTime;
    cpuTime_t currentSystemIdleTime;
    cpuTime_t currentSystemKernelTime;
    cpuTime_t currentSystemUserTime;

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

    float appCpuPercentage = 0.0f;
    float systemCpuPercentage = 0.0f;

#ifdef Q_OS_WIN
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

    if (totalSystemCpuTimeDelta > 0/* && processors > 0*/)
        appCpuPercentage = (static_cast<float>(processTotalTimeDelta) / totalSystemCpuTimeDelta) * 100.0f;// / processors;

    if (totalSystemCpuTimeDelta > 0)
        systemCpuPercentage = (static_cast<float>(systemTotalTimeDelta) / totalSystemCpuTimeDelta) * 100.0f;
#else

    // System CPU usage
    cpuTime_t totalSystemCpuTime = currentSystemUserTime + currentSystemKernelTime + currentSystemIdleTime;
    cpuTime_t lastTotalSystemCpuTime = lastSystemIdleTime + lastSystemKernelTime + lastSystemUserTime;
    cpuTime_t totalSystemCpuTimeDelta = totalSystemCpuTime - lastTotalSystemCpuTime;
    cpuTime_t idleCpuTimeDelta = currentSystemIdleTime - lastSystemIdleTime;
    cpuTime_t nonIdleSystemCpuTimeDelta = totalSystemCpuTimeDelta - idleCpuTimeDelta;

    // Application CPU usage
    cpuTime_t processTimeDelta = (currentProcessUserTime + currentProcessKernelTime) - (lastProcessUserTime + lastProcessKernelTime);

    if (totalSystemCpuTimeDelta > 0)
    {
        systemCpuPercentage = static_cast<double>(nonIdleSystemCpuTimeDelta) / totalSystemCpuTimeDelta * 100.0f;
        appCpuPercentage = static_cast<float>(processTimeDelta) / static_cast<float>(totalSystemCpuTimeDelta) * 100.0f/* * m_numCores*/;
    }

#endif

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
bool CpuUsage::GetProcessTimes(cpuTime_t &kernelTime, cpuTime_t &userTime)
{
#ifdef Q_OS_WIN
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
#else
    QString procStatPath = QString("/proc/%1/stat").arg(m_pid);
    QFile file(procStatPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    QString line = in.readLine();
    file.close();

    // Use a more robust way to handle the process name with spaces
    // The process name is in parentheses and can contain spaces, so we parse it separately
    int closeParen = line.lastIndexOf(')');
    QString statusString = line.mid(closeParen + 1).trimmed();

    QStringList xparts = statusString.split(" ", Qt::SkipEmptyParts);

    // The values we need are at specific positions after the process name
    // (14th and 15th fields, which are at index 13 and 14 of the status string list)
    if (xparts.size() < 15)
        return false;

    cpuTime_t utime = xparts[11].toULongLong();
    cpuTime_t stime = xparts[12].toULongLong();

    kernelTime = stime;
    userTime = utime;

    return true;
#endif
}

/*
===================
CpuUsage::GetSystemTimes
===================
*/
bool CpuUsage::GetSystemTimes(cpuTime_t &idleTime, cpuTime_t &kernelTime, cpuTime_t &userTime)
{
#ifdef Q_OS_WIN
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
#else
    QFile file("/proc/stat");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    QString line = in.readLine();
    file.close();

    // The first line starts with "cpu" and contains the total CPU time
    QStringList parts = line.split(" ", Qt::SkipEmptyParts);
    if (parts.size() < 5)
        return false;

    cpuTime_t user = parts[1].toULongLong();
    cpuTime_t nice = parts[2].toULongLong();
    cpuTime_t system = parts[3].toULongLong();
    cpuTime_t idle = parts[4].toULongLong();

    idleTime = idle;
    kernelTime = system;
    userTime = user + nice;

    return true;
#endif
}

#ifdef Q_OS_WIN
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
#endif
