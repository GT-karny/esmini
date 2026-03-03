import { useState } from 'react';
import { QueryClient, QueryClientProvider, useQuery } from '@tanstack/react-query';
import { BrowserRouter, Routes, Route, NavLink, useLocation, useSearchParams } from 'react-router-dom';
import { CursorLight } from '@osce/theme-apex';
import { api } from './api/client';
import { ProjectsPage } from './pages/ProjectsPage';
import { ProjectDetailPage } from './pages/ProjectDetailPage';
import { ScenariosPage } from './pages/ScenariosPage';
import { NewSimulationPage } from './pages/NewSimulationPage';
import { SimulationsPage } from './pages/SimulationsPage';
import { SimulationDetailPage } from './pages/SimulationDetailPage';
import { SettingsPanel } from './components/SettingsPanel';

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
    <nav className="glass header-glow relative z-10 shrink-0">
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
            <NavLink to="/scenarios" className={linkClass}>Scenarios</NavLink>
            <NavLink to="/simulations" className={linkClass}>Jobs</NavLink>
            <NavLink to="/simulations/new" className={linkClass}>New Run</NavLink>
          </>
        )}

        <div className="ml-auto">
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
        </div>
      </div>
    </nav>
  );
}

export default function App() {
  const [settingsOpen, setSettingsOpen] = useState(false);

  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <CursorLight />
        <div className="h-screen flex flex-col overflow-hidden">
          <NavBar onSettingsClick={() => setSettingsOpen(true)} />
          <main className="flex-1 overflow-hidden">
            <Routes>
              <Route path="/" element={<div className="h-full overflow-y-auto px-6 py-6"><ProjectsPage /></div>} />
              <Route path="/projects/:projectId" element={<ProjectDetailPage />} />
              <Route path="/projects/:projectId/sim/new" element={<div className="h-full overflow-y-auto px-6 py-6"><NewSimulationPage /></div>} />
              <Route path="/scenarios" element={<div className="h-full overflow-y-auto px-6 py-6"><ScenariosPage /></div>} />
              <Route path="/simulations" element={<div className="h-full overflow-y-auto px-6 py-6"><SimulationsPage /></div>} />
              <Route path="/simulations/new" element={<div className="h-full overflow-y-auto px-6 py-6"><NewSimulationPage /></div>} />
              <Route path="/simulations/:jobId" element={<div className="h-full overflow-y-auto px-6 py-6"><SimulationDetailPage /></div>} />
            </Routes>
          </main>
          <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
        </div>
      </BrowserRouter>
    </QueryClientProvider>
  );
}
