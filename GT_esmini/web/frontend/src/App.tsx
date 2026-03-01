import { useState } from 'react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom';
import { CursorLight } from '@osce/theme-apex';
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
  const linkClass = ({ isActive }: { isActive: boolean }) =>
    `apex-tab px-3 py-2 text-sm font-medium transition-colors ${
      isActive
        ? 'bg-glass-active text-foreground'
        : 'text-text-secondary hover:text-foreground hover:bg-glass-hover'
    }`;

  return (
    <nav className="glass header-glow relative z-10">
      <div className="max-w-7xl mx-auto px-4 flex items-center h-14 gap-1">
        <span className="font-display text-foreground font-bold text-lg tracking-wider mr-6">
          GT-SIM
        </span>
        <NavLink to="/" className={linkClass} end>Projects</NavLink>
        <NavLink to="/scenarios" className={linkClass}>Scenarios</NavLink>
        <NavLink to="/simulations" className={linkClass}>Jobs</NavLink>
        <NavLink to="/simulations/new" className={linkClass}>New Run</NavLink>
        <button
          onClick={onSettingsClick}
          className="ml-auto p-2 text-text-secondary hover:text-foreground hover:bg-glass-hover transition-colors cursor-pointer"
          aria-label="Settings"
        >
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" strokeWidth={1.5} stroke="currentColor" className="w-5 h-5">
            <path strokeLinecap="round" strokeLinejoin="round" d="M9.594 3.94c.09-.542.56-.94 1.11-.94h2.593c.55 0 1.02.398 1.11.94l.213 1.281c.063.374.313.686.645.87.074.04.147.083.22.127.325.196.72.257 1.075.124l1.217-.456a1.125 1.125 0 0 1 1.37.49l1.296 2.247a1.125 1.125 0 0 1-.26 1.431l-1.003.827c-.293.241-.438.613-.43.992a7.723 7.723 0 0 1 0 .255c-.008.378.137.75.43.991l1.004.827c.424.35.534.955.26 1.43l-1.298 2.247a1.125 1.125 0 0 1-1.369.491l-1.217-.456c-.355-.133-.75-.072-1.076.124a6.47 6.47 0 0 1-.22.128c-.331.183-.581.495-.644.869l-.213 1.281c-.09.543-.56.94-1.11.94h-2.594c-.55 0-1.019-.398-1.11-.94l-.213-1.281c-.062-.374-.312-.686-.644-.87a6.52 6.52 0 0 1-.22-.127c-.325-.196-.72-.257-1.076-.124l-1.217.456a1.125 1.125 0 0 1-1.369-.49l-1.297-2.247a1.125 1.125 0 0 1 .26-1.431l1.004-.827c.292-.24.437-.613.43-.991a6.932 6.932 0 0 1 0-.255c.007-.38-.138-.751-.43-.992l-1.004-.827a1.125 1.125 0 0 1-.26-1.43l1.297-2.247a1.125 1.125 0 0 1 1.37-.491l1.216.456c.356.133.751.072 1.076-.124.072-.044.146-.086.22-.128.332-.183.582-.495.644-.869l.214-1.28Z" />
            <path strokeLinecap="round" strokeLinejoin="round" d="M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0Z" />
          </svg>
        </button>
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
        <div className="min-h-screen">
          <NavBar onSettingsClick={() => setSettingsOpen(true)} />
          <main className="max-w-7xl mx-auto px-4 py-6">
            <Routes>
              <Route path="/" element={<ProjectsPage />} />
              <Route path="/projects/:projectId" element={<ProjectDetailPage />} />
              <Route path="/projects/:projectId/sim/new" element={<NewSimulationPage />} />
              <Route path="/scenarios" element={<ScenariosPage />} />
              <Route path="/simulations" element={<SimulationsPage />} />
              <Route path="/simulations/new" element={<NewSimulationPage />} />
              <Route path="/simulations/:jobId" element={<SimulationDetailPage />} />
            </Routes>
          </main>
          <SettingsPanel open={settingsOpen} onClose={() => setSettingsOpen(false)} />
        </div>
      </BrowserRouter>
    </QueryClientProvider>
  );
}
