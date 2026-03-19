import { useState, useEffect } from 'react';
import { useQuery } from '@tanstack/react-query';
import { api } from '../api/client';

/**
 * Polls a running simulation job and auto-clears the ID
 * after the job reaches a terminal state (with a 2s delay).
 */
export function useJobPolling() {
  const [runningJobId, setRunningJobId] = useState<string | null>(null);
  const [latestJobId, setLatestJobId] = useState<string | null>(null);

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
