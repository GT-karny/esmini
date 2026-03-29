import { useState, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';

/**
 * Polls a running simulation job and auto-clears the ID
 * after the job reaches a terminal state (with a 2s delay).
 *
 * Also detects externally-started jobs (e.g. via API) by polling
 * the simulations list when no local job is tracked.
 */
export function useJobPolling() {
  const [runningJobId, setRunningJobId] = useState<string | null>(null);
  const [latestJobId, setLatestJobId] = useState<string | null>(null);

  // --- Detect externally-started running jobs ---
  const { data: activeJobs } = useQuery({
    queryKey: ['simulations-active'],
    queryFn: () => api.getSimulations('running', 1),
    // Only poll when we are NOT already tracking a job
    enabled: !runningJobId,
    refetchInterval: 2000,
  });

  // Adopt an externally-started running job
  useEffect(() => {
    if (!runningJobId && activeJobs && activeJobs.jobs.length > 0) {
      const job = activeJobs.jobs[0];
      setRunningJobId(job.job_id);
      setLatestJobId(job.job_id);
    }
  }, [runningJobId, activeJobs]);

  // --- Poll the tracked job's status ---
  const { data: runningJobStatus } = useQuery({
    queryKey: ['simulation', runningJobId],
    queryFn: () => api.getSimulation(runningJobId!),
    enabled: !!runningJobId,
    refetchInterval: 1000,
  });

  // Auto-switch: when running job completes, clear after 2s
  useEffect(() => {
    if (!runningJobStatus) return;
    const s = runningJobStatus.status;
    if (s === 'completed' || s === 'failed' || s === 'cancelled' || s === 'timeout') {
      const timer = globalThis.setTimeout(() => {
        setRunningJobId(null);
      }, 2000);
      return () => globalThis.clearTimeout(timer);
    }
  }, [runningJobStatus]);

  const isJobActive = !!runningJobId && !!runningJobStatus &&
    (runningJobStatus.status === 'running' || runningJobStatus.status === 'queued');

  const handleRunning = (jobId: string) => {
    setRunningJobId(jobId);
    setLatestJobId(jobId);
  };

  return {
    runningJobId,
    setRunningJobId,
    latestJobId,
    runningJobStatus,
    isJobActive,
    handleRunning,
  };
}
