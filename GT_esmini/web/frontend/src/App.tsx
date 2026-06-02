import { useState } from 'react';
import { QueryClient, QueryClientProvider, useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { BrowserRouter, Routes, Route, NavLink, useLocation, useSearchParams } from 'react-router-dom';
import { CursorLight } from '@osce/theme-apex';
import { api } from './api/client';
import { ProjectsPage } from './pages/ProjectsPage';
import { ProjectDetailPage } from './pages/ProjectDetailPage';
import { SimulationsPage } from './pages/SimulationsPage';
import { SimulationDetailPage } from './pages/SimulationDetailPage';
import { VerificationReplayPage } from './pages/VerificationReplayPage';
import { VdLivePage } from './pages/VdLivePage';
import { SettingsPanel } from './components/SettingsPanel';
import { WindowControls, isElectron } from './components/WindowControls';

const queryClient = new QueryClient({
  defaultOptions: { queries: { staleTime: 5000 } },
});

function NavBar({ onSettingsClick }: { onSettingsClick: () => void }) {
  const location = useLocation();
  const [searchParams, setSearchParams] = useSearchParams();

  const projectMatch = location.pathname.match(/^\/projects\/([^/]+)$/);
  const isProjectPage = !!projectMatch;
  const projectId = projectMatch?.[1];

  const { data: project } = useQuery({
    queryKey: ['project', projectId],
    queryFn: () => api.getProject(projectId!),
    enabled: !!projectId,
  });

  const linkClass = ({ isActive }: { isActive: boolean }) =>
    `apex-tab px-3 py-2 text-sm font-medium transition-colors ${
      isActive
        ? 'bg-glass-active text-foreground'
        : 'text-text-secondary hover:text-foreground hover:bg-glass-hover'
    }`;

  const currentTab = searchParams.get('tab') ?? 'scenarios';

  const switchTab = (tab: string) => {
    const next = new URLSearchParams(searchParams);
    if (tab === 'scenarios') {
      next.delete('tab');
    } else {
      next.set('tab', tab);
    }
    setSearchParams(next, { replace: true });
  };

  return (
    <nav className={`glass header-glow relative z-10 shrink-0${isElectron ? ' app-drag' : ''}`}>
      <div className="px-6 flex items-center h-11 gap-1">
        <NavLink to="/" className="flex items-center gap-2 font-display text-foreground font-bold text-base tracking-wider mr-4">
          <svg viewBox="0 0 20 20" fill="none" className="w-5 h-5 shrink-0" aria-hidden="true">
            {/* Road surface */}
            <path d="M6 16L9 3h2l3 13" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" opacity="0.5" />
            {/* Center dashed line */}
            <path d="M10 5v2.5M10 9.5v2.5M10 14v2" stroke="var(--color-primary, #7B88E8)" strokeWidth="0.8" strokeLinecap="round" />
            {/* Vehicle */}
            <rect x="7.5" y="7" width="5" height="3" rx="0.8" fill="var(--color-primary, #7B88E8)" opacity="0.85" />
          </svg>
          GT-esmini
        </NavLink>

        {isProjectPage ? (
          <>
            <NavLink to="/" className="text-text-secondary hover:text-foreground text-sm transition-colors">
              Projects
            </NavLink>
            <span className="text-text-tertiary text-sm mx-1">/</span>
            <span className="text-foreground text-sm font-medium truncate max-w-48">
              {project?.name ?? '...'}
            </span>
            {project?.is_builtin && (
              <span className="text-[10px] uppercase tracking-wider text-text-tertiary border border-glass-edge px-1.5 py-0.5 ml-1">
                read-only
              </span>
            )}

            <div className="flex gap-1 ml-6">
              {(['scenarios', 'files'] as const).map((t) => (
                <button
                  key={t}
                  onClick={() => switchTab(t)}
                  className={`apex-tab px-3 py-2 text-sm font-medium transition-colors cursor-pointer ${
                    currentTab === t
                      ? 'bg-glass-active text-foreground'
                      : 'text-text-secondary hover:text-foreground hover:bg-glass-hover'
                  }`}
                >
                  {t.charAt(0).toUpperCase() + t.slice(1)}
                </button>
              ))}
            </div>
          </>
        ) : (
          <>
            <NavLink to="/" className={linkClass} end>Projects</NavLink>
            <NavLink to="/simulations" className={linkClass}>Jobs</NavLink>
            <NavLink to="/verification" className={linkClass}>Verify</NavLink>
            <ProjectsRootIndicator />
          </>
        )}

        <div className="ml-auto flex items-center">
          {isProjectPage && (
            <NavLink
              to="/simulations"
              className={({ isActive }) =>
                `px-3 py-2 text-sm font-medium transition-colors ${
                  isActive
                    ? 'bg-glass-active text-foreground'
                    : 'text-text-secondary hover:text-foreground hover:bg-glass-hover'
                }`
              }
            >
              Jobs
            </NavLink>
          )}
          <button
            onClick={onSettingsClick}
            className="p-1.5 text-text-secondary hover:text-foreground hover:bg-glass-hover transition-colors cursor-pointer"
            aria-label="Settings"
          >
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" strokeWidth={1.5} stroke="currentColor" className="w-4 h-4">
              <path strokeLinecap="round" strokeLinejoin="round" d="M9.594 3.94c.09-.542.56-.94 1.11-.94h2.593c.55 0 1.02.398 1.11.94l.213 1.281c.063.374.313.686.645.87.074.04.147.083.22.127.325.196.72.257 1.075.124l1.217-.456a1.125 1.125 0 0 1 1.37.49l1.296 2.247a1.125 1.125 0 0 1-.26 1.431l-1.003.827c-.293.241-.438.613-.43.992a7.723 7.723 0 0 1 0 .255c-.008.378.137.75.43.991l1.004.827c.424.35.534.955.26 1.43l-1.298 2.247a1.125 1.125 0 0 1-1.369.491l-1.217-.456c-.355-.133-.75-.072-1.076.124a6.47 6.47 0 0 1-.22.128c-.331.183-.581.495-.644.869l-.213 1.281c-.09.543-.56.94-1.11.94h-2.594c-.55 0-1.019-.398-1.11-.94l-.213-1.281c-.062-.374-.312-.686-.644-.87a6.52 6.52 0 0 1-.22-.127c-.325-.196-.72-.257-1.076-.124l-1.217.456a1.125 1.125 0 0 1-1.369-.49l-1.297-2.247a1.125 1.125 0 0 1 .26-1.431l1.004-.827c.292-.24.437-.613.43-.991a6.932 6.932 0 0 1 0-.255c.007-.38-.138-.751-.43-.992l-1.004-.827a1.125 1.125 0 0 1-.26-1.43l1.297-2.247a1.125 1.125 0 0 1 1.37-.491l1.216.456c.356.133.751.072 1.076-.124.072-.044.146-.086.22-.128.332-.183.582-.495.644-.869l.214-1.28Z" />
              <path strokeLinecap="round" strokeLinejoin="round" d="M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0Z" />
            </svg>
          </button>
          <WindowControls />
        </div>
      </div>
    </nav>
  );
}

/* ---------- Projects Root (inline in navbar) ---------- */

function ProjectsRootIndicator() {
  const queryClient = useQueryClient();

  const { data } = useQuery({
    queryKey: ['projects-root'],
    queryFn: api.getProjectsRoot,
  });

  const mutation = useMutation({
    mutationFn: (root: string | null) => api.setProjectsRoot(root),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['projects-root'] });
      queryClient.invalidateQueries({ queryKey: ['projects'] });
    },
  });

  const handleBrowse = async () => {
    if (isElectron) {
      const selected = await window.electronAPI!.selectDirectory();
      if (selected) mutation.mutate(selected);
    }
  };

  const handleClear = () => mutation.mutate(null);

  const displayPath = data?.projects_root ?? data?.effective_dir ?? '';
  // Show only the last 2 path segments for compact display
  const shortPath = displayPath
    ? displayPath.replace(/\\/g, '/').split('/').slice(-2).join('/')
    : '';

  return (
    <div className="flex items-center gap-1.5 ml-4 text-xs text-text-tertiary no-drag">
      <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" strokeWidth={1.5} stroke="currentColor" className="w-3.5 h-3.5 shrink-0">
        <path strokeLinecap="round" strokeLinejoin="round" d="M2.25 12.75V12A2.25 2.25 0 0 1 4.5 9.75h15A2.25 2.25 0 0 1 21.75 12v.75m-8.69-6.44-2.12-2.12a1.5 1.5 0 0 0-1.061-.44H4.5A2.25 2.25 0 0 0 2.25 6v12a2.25 2.25 0 0 0 2.25 2.25h15A2.25 2.25 0 0 0 21.75 18V9a2.25 2.25 0 0 0-2.25-2.25h-5.379a1.5 1.5 0 0 1-1.06-.44Z" />
      </svg>
      <span className="truncate max-w-48" title={displayPath}>{shortPath || 'Default'}</span>
      {isElectron && (
        <button
          onClick={handleBrowse}
          disabled={mutation.isPending}
          className="hover:text-foreground transition-colors cursor-pointer"
          title="Change projects root folder"
        >
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" strokeWidth={1.5} stroke="currentColor" className="w-3.5 h-3.5">
            <path strokeLinecap="round" strokeLinejoin="round" d="m16.862 4.487 1.687-1.688a1.875 1.875 0 1 1 2.652 2.652L6.832 19.82a4.5 4.5 0 0 1-1.897 1.13l-2.685.8.8-2.685a4.5 4.5 0 0 1 1.13-1.897L16.863 4.487Zm0 0L19.5 7.125" />
          </svg>
        </button>
      )}
      {data?.is_custom && (
        <button
          onClick={handleClear}
          disabled={mutation.isPending}
          className="hover:text-foreground transition-colors cursor-pointer"
          title="Reset to default"
        >
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" strokeWidth={1.5} stroke="currentColor" className="w-3.5 h-3.5">
            <path strokeLinecap="round" strokeLinejoin="round" d="M6 18 18 6M6 6l12 12" />
          </svg>
        </button>
      )}
    </div>
  );
}

// Main app shell (NavBar + nested routes + settings). Rendered for every route
// except the layout-less standalone windows under /live/*.
function AppShell() {
  const [settingsOpen, setSettingsOpen] = useState(false);
  return (
    <div className="h-screen flex flex-col overflow-hidden">
      <NavBar onSettingsClick={() => setSettingsOpen(true)} />
      <main className="flex-1 overflow-hidden">
        <Routes>
          <Route path="/" element={<div className="h-full overflow-y-auto px-6 py-6"><ProjectsPage /></div>} />
          <Route path="/projects/:projectId" element={<ProjectDetailPage />} />
          <Route path="/simulations" element={<div className="h-full overflow-y-auto px-6 py-6"><SimulationsPage /></div>} />
          <Route path="/simulations/:jobId" element={<div className="h-full overflow-y-auto px-6 py-6"><SimulationDetailPage /></div>} />
          <Route path="/verification" element={<VerificationReplayPage />} />
        </Routes>
      </main>
      <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
    </div>
  );
}

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <CursorLight />
        <Routes>
          {/* Standalone window (no NavBar) — opened via window.open() */}
          <Route path="/live/vd/:jobId" element={<VdLivePage />} />
          <Route path="/*" element={<AppShell />} />
        </Routes>
      </BrowserRouter>
    </QueryClientProvider>
  );
}
