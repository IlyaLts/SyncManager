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

    bool GetProcessTimes(ULONGLONG &kernelTime, ULONGLONG &userTime);
    bool GetSystemTimes(ULONGLONG &idleTime, ULONGLONG &kernelTime, ULONGLONG &userTime);
    ULONGLONG FileTimeToULONGLONG(const FILETIME &ft);

    // Process CPU usage
    HANDLE process = nullptr;
    ULONGLONG lastProcessKernelTime = 0;
    ULONGLONG lastProcessUserTime = 0;

    // System CPU usage
    ULONGLONG lastSystemIdleTime = 0;
    ULONGLONG lastSystemKernelTime = 0;
    ULONGLONG lastSystemUserTime = 0;

    QTimer *timer;
    int processors = 0;
    int interval;
};

#endif // CPUUSAGE_H
