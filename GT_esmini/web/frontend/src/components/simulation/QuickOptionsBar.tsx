import { IconToggle } from '../ui/Input';

export interface QuickOptionsBarProps {
  headless: boolean;
  setHeadless: (v: boolean) => void;
  record: boolean;
  setRecord: (v: boolean) => void;
  noRealtime: boolean;
  setNoRealtime: (v: boolean) => void;
  autolight: boolean;
  setAutolight: (v: boolean) => void;
}

const IconWindow = ({ headless }: { headless: boolean }) =>
  headless ? (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M2 3h12v8H2V3zm1 1v6h10V4H3zm2 8h6v1H5v-1z" />
      <path d="M1.5 1.5l13 13" stroke="currentColor" strokeWidth="1.5" fill="none" />
    </svg>
  ) : (
    <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
      <path d="M2 3h12v8H2V3zm1 1v6h10V4H3zm2 8h6v1H5v-1z" />
    </svg>
  );

const IconRecord = () => (
  <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
    <circle cx="8" cy="8" r="5" />
  </svg>
);

const IconFastForward = () => (
  <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
    <path d="M2 3l6 5-6 5V3zm6 0l6 5-6 5V3z" />
  </svg>
);

const IconAutoLight = () => (
  <svg viewBox="0 0 16 16" fill="currentColor" className="w-4 h-4">
    <path d="M8 1C5.5 1 4 3 4 5.5c0 1.5.7 2.8 1.5 3.5.5.5.5 1 .5 1.5V12h4v-1.5c0-.5 0-1 .5-1.5.8-.7 1.5-2 1.5-3.5C12 3 10.5 1 8 1zm-1 12h2v1H7v-1z" />
  </svg>
);

export function QuickOptionsBar({
  headless,
  setHeadless,
  record,
  setRecord,
  noRealtime,
  setNoRealtime,
  autolight,
  setAutolight,
}: QuickOptionsBarProps) {
  return (
    <div className="flex items-center gap-2">
      <IconToggle icon={<IconWindow headless={headless} />} label="Window" active={!headless} onChange={(v) => setHeadless(!v)} />
      <IconToggle icon={<IconRecord />} label="Record" active={record} onChange={setRecord} />
      <IconToggle icon={<IconFastForward />} label="No Realtime" active={noRealtime} onChange={setNoRealtime} />
      <IconToggle icon={<IconAutoLight />} label="AutoLight" active={autolight} onChange={setAutolight} />
    </div>
  );
}
