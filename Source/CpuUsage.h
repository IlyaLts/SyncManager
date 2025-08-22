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

#ifndef CPUUSAGE_H
#define CPUUSAGE_H

#include <QObject>
#include <QTimer>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>

using cpuTime_t = ULONGLONG;
#else
using cpuTime_t = unsigned long long;
#endif

/*
===========================================================

    CpuUsage

===========================================================
*/
class CpuUsage : public QObject
{
    Q_OBJECT

public:

    explicit CpuUsage(QObject *parent = nullptr);
    ~CpuUsage();

    void startMonitoring(int interval);
    void stopMonitoring();

Q_SIGNALS:

    void cpuUsageUpdated(float appPercentage, float systemPercentage);

private Q_SLOTS:
    void updateCpuUsage();

private:

    bool GetProcessTimes(cpuTime_t &kernelTime, cpuTime_t &userTime);
    bool GetSystemTimes(cpuTime_t &idleTime, cpuTime_t &kernelTime, cpuTime_t &userTime);

#ifdef Q_OS_WIN
    ULONGLONG FileTimeToULONGLONG(const FILETIME &ft);

    HANDLE process = nullptr;
#else
    pid_t m_pid;
#endif

    // Process CPU usage
    cpuTime_t lastProcessKernelTime = 0;
    cpuTime_t lastProcessUserTime = 0;

    // System CPU usage
    cpuTime_t lastSystemIdleTime = 0;
    cpuTime_t lastSystemKernelTime = 0;
    cpuTime_t lastSystemUserTime = 0;

    QTimer *timer;
    int processors = 1;
    int interval;
};

#endif // CPUUSAGE_H
