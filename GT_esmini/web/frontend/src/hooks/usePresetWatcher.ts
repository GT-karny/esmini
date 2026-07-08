import { useEffect, useRef, useState } from 'react';
import { useQueryClient } from '@tanstack/react-query';

interface PresetWatcherOptions {
  projectId: string;
  scenarioFile: string;
}

interface PresetWatcherResult {
  externalChangePending: boolean;
  reload: () => void;
}

/**
 * Subscribes to /ws/presets/{projectId}. When the YAML changes externally,
 * sets `externalChangePending=true` so the UI can surface a banner. Calling
 * `reload()` invalidates the presets query (no further confirmation).
 */
export function usePresetWatcher({
  projectId,
  scenarioFile,
}: PresetWatcherOptions): PresetWatcherResult {
  const queryClient = useQueryClient();
  const [externalChangePending, setExternalChangePending] = useState(false);

  const scenarioStemRef = useRef(stem(scenarioFile));
  useEffect(() => {
    scenarioStemRef.current = stem(scenarioFile);
  }, [scenarioFile]);

  useEffect(() => {
    if (!projectId) return;

    let ws: WebSocket | null = null;
    let cancelled = false;
    let retryTimer: ReturnType<typeof setTimeout> | null = null;
    let retryDelay = 1000;

    const open = () => {
      if (cancelled) return;
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      ws = new WebSocket(
        `${protocol}//${window.location.host}/ws/presets/${encodeURIComponent(projectId)}`,
      );

      ws.onopen = () => {
        retryDelay = 1000;
      };

      ws.onmessage = (event) => {
        let msg: { type?: string; scenario_stem?: string };
        try {
          msg = JSON.parse(event.data);
        } catch {
          return;
        }
        if (msg.type !== 'presets_changed') return;
        if (msg.scenario_stem && msg.scenario_stem !== scenarioStemRef.current) {
          // change targets a different scenario in this project
          return;
        }
        setExternalChangePending(true);
      };

      ws.onclose = () => {
        if (cancelled) return;
        retryTimer = setTimeout(open, retryDelay);
        retryDelay = Math.min(retryDelay * 2, 15000);
      };

      ws.onerror = () => {
        try {
          ws?.close();
        } catch {
          // ignore
        }
      };
    };

    open();
    return () => {
      cancelled = true;
      if (retryTimer) clearTimeout(retryTimer);
      try {
        ws?.close();
      } catch {
        // ignore
      }
    };
  }, [projectId, scenarioFile, queryClient]);

  const reload = () => {
    setExternalChangePending(false);
    queryClient.invalidateQueries({
      queryKey: ['presets', projectId, scenarioFile],
    });
  };

  return { externalChangePending, reload };
}

function stem(scenarioFile: string): string {
  if (!scenarioFile) return '';
  const base = scenarioFile.split(/[\\/]/).pop() ?? scenarioFile;
  const dot = base.lastIndexOf('.');
  return dot > 0 ? base.slice(0, dot) : base;
}
