/****************************************************************************
**
** Copyright (C) 2016 Paul Lemire <paul.lemire350@gmail.com>
**
** This file is part of the Qt3D Profiler
**
** $QT_BEGIN_LICENSE:GPL-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: GPL-3.0
**
****************************************************************************/

#include "jobstatsreader.h"
#include "datamodels.h"

#include <QFile>
#include <QUrl>
#include <QDebug>

QHash<int, QString> JobStatsReader::jobTypeToNameTable;
QHash<int, QColor> JobStatsReader::jobTypeToColorTable;

JobTraces JobStatsReader::readTraceFile(const QUrl &fileUrl)
{
    QFile file(fileUrl.toLocalFile());
    JobTraces traceEntry;

    traceEntry.m_jobModel.reset(new JobModel);
    std::vector<Job> frameJobs;

    if (file.open(QFile::ReadOnly)) {
        const int sizeOfHeader = sizeof(FrameHeader);
        const int sizeOfJob = sizeof(JobRunStats);
        QByteArray headerBuffer(sizeOfHeader, '\0');
        QByteArray jobBuffer(sizeOfJob, '\0');

        traceEntry.m_title = file.fileName();

        QVector<quint64> threadIds;
        // Read header
        uint readSize = 0;
        // Check file is at least as long as what header says
        while ((readSize = file.read(headerBuffer.data(), sizeOfHeader)) == sizeOfHeader) {
            FrameHeader *header = reinterpret_cast<FrameHeader *>(headerBuffer.data());

            // Read commands
            uint c = 0;
            std::vector<Job> jobs;
            while (c < header->jobCount && (readSize = file.read(jobBuffer.data(), sizeOfJob)) == sizeOfJob) {
                JobRunStats *jobStat = reinterpret_cast<JobRunStats *>(jobBuffer.data());
                Job job;
                job.m_jobStats = *jobStat;
                job.m_color = JobStatsReader::jobTypeToColorTable.value(jobStat->jobId.typeAndInstance[0], QColor(Qt::red));
                job.m_name = JobStatsReader::jobTypeToNameTable.value(jobStat->jobId.typeAndInstance[0], QLatin1String("Unknown"));
                jobs.push_back(job);
                ++c;
            }

            // Sort Jobs by thread id and start time
            std::sort(jobs.begin(), jobs.end(), [] (const Job &a, const Job &b) {
                return a.m_jobStats.threadId < b.m_jobStats.threadId;
            });

            for (int i = 1, m = jobs.size(); i < m; ++i) {
                const int prec = i - 1;
                while (i < m && jobs[prec].m_jobStats.threadId == jobs[i].m_jobStats.threadId)
                    ++i;
                if (i - prec > 1) {
                    std::sort(jobs.begin() + prec, jobs.begin() + i, [] (const Job &a, const Job &b) {
                        return a.m_jobStats.startTime < b.m_jobStats.startTime;
                    });
                }
            }

            qint64 startTime = std::numeric_limits<qint64>::max();
            qint64 endTime = std::numeric_limits<qint64>::min();

            // Get minimum start and maximum end time
            for (const Job &job : jobs) {
                endTime = std::max(job.m_jobStats.endTime, endTime);
                startTime = std::min(job.m_jobStats.startTime, startTime);
            }

            if (startTime == std::numeric_limits<qint64>::max())
                startTime = 0;
            if (endTime == std::numeric_limits<qint64>::min())
                endTime = startTime;

            traceEntry.m_totalDuration += (endTime - startTime);

            // Compute thread count base on thread ids
            for (int i = 0, m = jobs.size(); i < m;) {
                const int j = i;
                while (i < m && jobs[j].m_jobStats.threadId == jobs[i].m_jobStats.threadId)
                    ++i;
                if (!threadIds.contains(jobs[j].m_jobStats.threadId))
                    threadIds.push_back(jobs[j].m_jobStats.threadId);
            }

            for (int i = 0, m = jobs.size(); i < m; ++i) {
                Job &job = jobs[i];
                job.m_frameStart = job.m_jobStats.startTime;
                job.m_frameEnd = job.m_jobStats.endTime;
                job.m_relativeStart = (job.m_jobStats.startTime - startTime);
                job.m_relativeEnd = (job.m_jobStats.endTime - startTime);
                job.m_x = job.m_frameStart * 0.000001;
                job.m_threadId = threadIds.indexOf(job.m_jobStats.threadId) + 1;
            }

           frameJobs.insert(frameJobs.end(), jobs.begin(), jobs.end());
        } // Repeat

        qreal maxStartTime = std::numeric_limits<qreal>::max();
        for (const Job &job : frameJobs)
            if (job.m_frameStart < maxStartTime)
                maxStartTime = job.m_frameStart;

        traceEntry.m_threadCount = threadIds.size();
        traceEntry.m_jobModel->insertRows(std::move(frameJobs));
        traceEntry.m_startTime = maxStartTime;

        qDebug() << Q_FUNC_INFO << "Done";
    } else {
        qDebug() << Q_FUNC_INFO << "Failure to open";
    }
    return traceEntry;
}
