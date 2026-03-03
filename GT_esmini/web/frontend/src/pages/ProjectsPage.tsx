import { useState, useRef, useCallback } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useNavigate } from 'react-router-dom';
import { GlassPanel } from '@osce/theme-apex';
import { api, type Project } from '../api/client';
import { Button } from '../components/ui/Button';
import { TextInput } from '../components/ui/Input';
import { ErrorPanel } from '../components/ui/ErrorPanel';

export function ProjectsPage() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const [showCreate, setShowCreate] = useState(false);
  const [showUpload, setShowUpload] = useState(false);

  const { data: projects, isLoading, error, refetch } = useQuery({
    queryKey: ['projects'],
    queryFn: api.getProjects,
  });

  return (
    <div>
      <div className="flex items-center justify-between mb-8">
        <h1 className="text-2xl font-display font-bold tracking-wider">PROJECTS</h1>
        <div className="flex gap-2">
          <Button variant="secondary" size="md" onClick={() => setShowUpload(true)}>
            Upload ZIP
          </Button>
          <a href={api.getProjectTemplateUrl()} download>
            <Button variant="ghost" size="md">Template</Button>
          </a>
          <Button variant="primary" size="md" onClick={() => setShowCreate(true)}>
            + New
          </Button>
        </div>
      </div>

      {error && <ErrorPanel error={error} onRetry={() => refetch()} />}

      {isLoading && <ProjectGridSkeleton />}

      {projects && projects.length === 0 && (
        <GlassPanel className="p-12 text-center">
          <p className="text-text-secondary mb-4">No projects found.</p>
          <Button variant="primary" onClick={() => setShowCreate(true)}>
            Create your first project
          </Button>
        </GlassPanel>
      )}

      {projects && projects.length > 0 && (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
          {projects.map((p, i) => (
            <ProjectCard
              key={p.project_id}
              project={p}
              delay={i}
              onClick={() => navigate(`/projects/${p.project_id}`)}
            />
          ))}
        </div>
      )}

      <CreateProjectDialog
        open={showCreate}
        onClose={() => setShowCreate(false)}
        onCreated={(id) => {
          setShowCreate(false);
          queryClient.invalidateQueries({ queryKey: ['projects'] });
          navigate(`/projects/${id}`);
        }}
      />

      <UploadProjectDialog
        open={showUpload}
        onClose={() => setShowUpload(false)}
        onUploaded={(id) => {
          setShowUpload(false);
          queryClient.invalidateQueries({ queryKey: ['projects'] });
          navigate(`/projects/${id}`);
        }}
      />
    </div>
  );
}

/* ---------- Project Card ---------- */

function ProjectCard({
  project,
  delay,
  onClick,
}: {
  project: Project;
  delay: number;
  onClick: () => void;
}) {
  const delayClass = delay < 6 ? `d${delay + 1}` : 'd6';

  return (
    <div className={`enter ${delayClass}`} onClick={onClick}>
      <GlassPanel
        className="p-5 cursor-pointer hover:border-glass-edge-mid transition-colors h-full"
      >
        <div className="flex items-start justify-between mb-3">
          <h3 className="font-display font-bold text-sm tracking-wide">
            {project.is_builtin && (
              <span className="text-warning mr-1.5" title="Built-in">&#9733;</span>
            )}
            {project.name}
          </h3>
          {project.is_builtin && (
            <span className="text-[10px] uppercase tracking-wider text-text-tertiary border border-glass-edge px-1.5 py-0.5">
              read-only
            </span>
          )}
        </div>

        {project.description && (
          <p className="text-text-secondary text-xs mb-3 line-clamp-2">{project.description}</p>
        )}

        <div className="flex gap-4 text-xs text-text-secondary font-mono mt-auto">
          <span>{project.scenario_count} scenario{project.scenario_count !== 1 ? 's' : ''}</span>
          {project.road_count > 0 && (
            <span>{project.road_count} road{project.road_count !== 1 ? 's' : ''}</span>
          )}
        </div>

        <div className="text-[10px] text-text-tertiary mt-2">
          Updated {formatRelativeTime(project.updated_at)}
        </div>
      </GlassPanel>
    </div>
  );
}

/* ---------- Create Dialog ---------- */

function CreateProjectDialog({
  open,
  onClose,
  onCreated,
}: {
  open: boolean;
  onClose: () => void;
  onCreated: (id: string) => void;
}) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');

  const mutation = useMutation({
    mutationFn: () => api.createProject(name.trim(), description.trim()),
    onSuccess: (data) => {
      setName('');
      setDescription('');
      onCreated(data.project_id);
    },
  });

  if (!open) return null;

  return (
    <DialogOverlay onClose={onClose}>
      <GlassPanel className="p-6 max-w-md w-full">
        <h2 className="text-lg font-display font-bold mb-4">New Project</h2>
        <div className="space-y-3">
          <TextInput
            label="Name"
            placeholder="My Scenario Set"
            value={name}
            onChange={(e) => setName(e.target.value)}
            autoFocus
          />
          <TextInput
            label="Description (optional)"
            placeholder="Brief description..."
            value={description}
            onChange={(e) => setDescription(e.target.value)}
          />
        </div>
        <p className="text-text-tertiary text-xs mt-3">
          Need a starting point?{' '}
          <a
            href={api.getProjectTemplateUrl()}
            download
            className="text-primary hover:underline"
          >
            Download project template
          </a>
        </p>
        {mutation.error && (
          <p className="text-destructive text-xs mt-3">
            {mutation.error instanceof Error ? mutation.error.message : 'Failed to create project'}
          </p>
        )}
        <div className="flex justify-end gap-3 mt-6">
          <Button variant="ghost" size="sm" onClick={onClose}>Cancel</Button>
          <Button
            variant="primary"
            size="sm"
            disabled={!name.trim() || mutation.isPending}
            onClick={() => mutation.mutate()}
          >
            {mutation.isPending ? 'Creating...' : 'Create'}
          </Button>
        </div>
      </GlassPanel>
    </DialogOverlay>
  );
}

/* ---------- Upload Dialog ---------- */

function UploadProjectDialog({
  open,
  onClose,
  onUploaded,
}: {
  open: boolean;
  onClose: () => void;
  onUploaded: (id: string) => void;
}) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [file, setFile] = useState<File | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const mutation = useMutation({
    mutationFn: () => api.uploadProject(file!, name.trim(), description.trim()),
    onSuccess: (data) => {
      setName('');
      setDescription('');
      setFile(null);
      onUploaded(data.project_id);
    },
  });

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    setDragOver(false);
    const dropped = e.dataTransfer.files[0];
    if (dropped && dropped.name.endsWith('.zip')) {
      setFile(dropped);
      if (!name.trim()) {
        setName(dropped.name.replace(/\.zip$/i, ''));
      }
    }
  }, [name]);

  if (!open) return null;

  return (
    <DialogOverlay onClose={onClose}>
      <GlassPanel className="p-6 max-w-md w-full">
        <h2 className="text-lg font-display font-bold mb-4">Upload Project</h2>

        {/* Drop zone */}
        <div
          className={`border-2 border-dashed p-8 text-center cursor-pointer transition-colors mb-4 ${
            dragOver
              ? 'border-primary bg-primary/5'
              : file
                ? 'border-success/40 bg-success/5'
                : 'border-glass-edge hover:border-glass-edge-mid'
          }`}
          onDragOver={(e) => { e.preventDefault(); setDragOver(true); }}
          onDragLeave={() => setDragOver(false)}
          onDrop={handleDrop}
          onClick={() => fileInputRef.current?.click()}
        >
          <input
            ref={fileInputRef}
            type="file"
            accept=".zip"
            className="hidden"
            onChange={(e) => {
              const f = e.target.files?.[0];
              if (f) {
                setFile(f);
                if (!name.trim()) setName(f.name.replace(/\.zip$/i, ''));
              }
            }}
          />
          {file ? (
            <div>
              <p className="text-success text-sm font-mono">{file.name}</p>
              <p className="text-text-tertiary text-xs mt-1">{(file.size / 1024 / 1024).toFixed(1)} MB</p>
            </div>
          ) : (
            <div>
              <p className="text-text-secondary text-sm">Drop a .zip file here or click to browse</p>
              <p className="text-text-tertiary text-xs mt-1">Contains xosc, xodr, and related files</p>
            </div>
          )}
        </div>

        <div className="space-y-3">
          <TextInput
            label="Project Name"
            placeholder="My Scenario Set"
            value={name}
            onChange={(e) => setName(e.target.value)}
          />
          <TextInput
            label="Description (optional)"
            placeholder="Brief description..."
            value={description}
            onChange={(e) => setDescription(e.target.value)}
          />
        </div>

        {mutation.error && (
          <p className="text-destructive text-xs mt-3">
            {mutation.error instanceof Error ? mutation.error.message : 'Upload failed'}
          </p>
        )}

        <div className="flex justify-end gap-3 mt-6">
          <Button variant="ghost" size="sm" onClick={onClose}>Cancel</Button>
          <Button
            variant="primary"
            size="sm"
            disabled={!file || !name.trim() || mutation.isPending}
            onClick={() => mutation.mutate()}
          >
            {mutation.isPending ? 'Uploading...' : 'Upload'}
          </Button>
        </div>
      </GlassPanel>
    </DialogOverlay>
  );
}

/* ---------- Dialog overlay (reusable) ---------- */

function DialogOverlay({
  onClose,
  children,
}: {
  onClose: () => void;
  children: React.ReactNode;
}) {
  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center">
      <div className="absolute inset-0 bg-black/60" onClick={onClose} />
      <div className="relative z-10">{children}</div>
    </div>
  );
}

/* ---------- Skeleton ---------- */

function ProjectGridSkeleton() {
  return (
    <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
      {Array.from({ length: 6 }).map((_, i) => (
        <GlassPanel key={i} className="p-5">
          <div className="h-4 bg-glass-1 animate-pulse w-32 mb-3" />
          <div className="h-3 bg-glass-1 animate-pulse w-48 mb-2" />
          <div className="h-3 bg-glass-1 animate-pulse w-24" />
        </GlassPanel>
      ))}
    </div>
  );
}

/* ---------- Helpers ---------- */

function formatRelativeTime(iso: string): string {
  const diff = Date.now() - new Date(iso).getTime();
  const mins = Math.floor(diff / 60000);
  if (mins < 1) return 'just now';
  if (mins < 60) return `${mins}m ago`;
  const hours = Math.floor(mins / 60);
  if (hours < 24) return `${hours}h ago`;
  const days = Math.floor(hours / 24);
  if (days < 30) return `${days}d ago`;
  return new Date(iso).toLocaleDateString();
}
