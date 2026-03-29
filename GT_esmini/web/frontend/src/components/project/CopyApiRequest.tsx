import { useState, useEffect } from 'react';
import type { SimulationRequest, ScenarioParam } from '../../api/client';
import { Button } from '../ui/Button';

const STORAGE_KEY = 'gt_api_base_url';
const DEFAULT_BASE = 'http://127.0.0.1:8000';

interface CopyApiRequestProps {
  getRequest: () => SimulationRequest | null;
  scenarioParams: ScenarioParam[];
  paramSource: Record<string, string>;
}

export function CopyApiRequest({ getRequest, scenarioParams, paramSource }: CopyApiRequestProps) {
  const [baseUrl, setBaseUrl] = useState(() => localStorage.getItem(STORAGE_KEY) || DEFAULT_BASE);
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    localStorage.setItem(STORAGE_KEY, baseUrl);
  }, [baseUrl]);

  const handleCopy = async () => {
    const request = getRequest();
    if (!request) return;

    // Full expansion: include all scenario params with current values
    if (scenarioParams.length > 0) {
      const allParams: Record<string, string> = {};
      for (const p of scenarioParams) {
        allParams[p.name] = paramSource[p.name] ?? p.value;
      }
      request.param_overrides = allParams;
    }

    const json = JSON.stringify(request, null, 2);
    try {
      await navigator.clipboard.writeText(json);
      setCopied(true);
    } catch {
      console.error('Clipboard write failed');
    }
  };

  useEffect(() => {
    if (!copied) return;
    const timer = window.setTimeout(() => setCopied(false), 2000);
    return () => clearTimeout(timer);
  }, [copied]);

  return (
    <div className="space-y-2">
      <h4 className="text-xs font-display font-medium text-text-tertiary uppercase tracking-wider">
        API Request
      </h4>

      <div className="space-y-1">
        <label className="flex items-center gap-2 text-xs text-text-secondary">
          <span className="shrink-0">Base URL</span>
          <input
            type="text"
            value={baseUrl}
            onChange={(e) => setBaseUrl(e.target.value)}
            className="flex-1 min-w-0 px-2 py-0.5 rounded text-xs font-mono bg-glass-bg border border-glass-edge text-text-primary focus:outline-none focus:border-accent"
          />
        </label>
        <p className="text-[11px] font-mono text-text-tertiary">
          POST {baseUrl}/api/simulations
        </p>
        <p className="text-[11px] font-mono text-text-tertiary">
          Content-Type: application/json
        </p>
      </div>

      <Button size="sm" variant="secondary" onClick={handleCopy} className="w-full">
        {copied ? '\u2713 Copied!' : 'Copy API Request'}
      </Button>
    </div>
  );
}
