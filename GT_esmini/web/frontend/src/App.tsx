import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { BrowserRouter, Routes, Route, NavLink } from 'react-router-dom';
import { ScenariosPage } from './pages/ScenariosPage';
import { NewSimulationPage } from './pages/NewSimulationPage';
import { SimulationsPage } from './pages/SimulationsPage';
import { SimulationDetailPage } from './pages/SimulationDetailPage';

const queryClient = new QueryClient({
  defaultOptions: { queries: { staleTime: 5000 } },
});

function NavBar() {
  const linkClass = ({ isActive }: { isActive: boolean }) =>
    `px-3 py-2 rounded text-sm font-medium transition-colors ${
      isActive
        ? 'bg-white/10 text-white'
        : 'text-gray-300 hover:text-white hover:bg-white/5'
    }`;

  return (
    <nav className="bg-gray-900 border-b border-gray-800">
      <div className="max-w-7xl mx-auto px-4 flex items-center h-14 gap-1">
        <span className="text-white font-bold text-lg mr-6">GT_Sim</span>
        <NavLink to="/" className={linkClass} end>Scenarios</NavLink>
        <NavLink to="/simulations" className={linkClass}>Jobs</NavLink>
        <NavLink to="/simulations/new" className={linkClass}>Run</NavLink>
      </div>
    </nav>
  );
}

export default function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <div className="min-h-screen bg-gray-950 text-gray-100">
          <NavBar />
          <main className="max-w-7xl mx-auto px-4 py-6">
            <Routes>
              <Route path="/" element={<ScenariosPage />} />
              <Route path="/simulations" element={<SimulationsPage />} />
              <Route path="/simulations/new" element={<NewSimulationPage />} />
              <Route path="/simulations/:jobId" element={<SimulationDetailPage />} />
            </Routes>
          </main>
        </div>
      </BrowserRouter>
    </QueryClientProvider>
  );
}
