import { useState, useRef, useCallback } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type ProjectDetail, type ProjectFile } from '../../api/client';
import { Button } from '../ui/Button';
import { TableShell, TableSkeleton } from '../ui/Table';
import { EmptyState } from '../ui/EmptyState';
import { ErrorPanel } from '../ui/ErrorPanel';
import { ConfirmDialog } from '../ui/ConfirmDialog';

function formatFileSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

export function FilesTab({ projectId, project }: { projectId: string; project: ProjectDetail }) {
  const queryClient = useQueryClient();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [deleteTarget, setDeleteTarget] = useState<string | null>(null);

  const { data: files, isLoading, error, refetch } = useQuery({
    queryKey: ['project-files', projectId],
    queryFn: () => api.getProjectFiles(projectId),
  });

  const uploadMut = useMutation({
    mutationFn: (file: File) => api.uploadProjectFile(projectId, file),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: ['project-files', projectId] }),
  });

  const deleteMut = useMutation({
    mutationFn: (path: string) => api.deleteProjectFile(projectId, path),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: ['project-files', projectId] });
      setDeleteTarget(null);
    },
  });

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    if (project.is_builtin) return;
    const dropped = e.dataTransfer.files;
    for (let i = 0; i < dropped.length; i++) {
      uploadMut.mutate(dropped[i]);
    }
  }, [project.is_builtin, uploadMut]);

  if (isLoading) return <TableSkeleton columns={4} rows={6} />;
  if (error) return <ErrorPanel error={error} onRetry={() => refetch()} />;

  const fileColumns = [
    { key: 'name', label: 'Name' },
    { key: 'type', label: 'Type' },
    { key: 'size', label: 'Size', align: 'right' as const },
    { key: 'actions', label: '', align: 'right' as const },
  ];

  return (
    <div
      onDragOver={(e) => { if (!project.is_builtin) e.preventDefault(); }}
      onDrop={handleDrop}
    >
      {/* Upload bar */}
      {!project.is_builtin && (
        <div className="flex items-center justify-between mb-4">
          <p className="text-text-secondary text-xs">
            Drag &amp; drop files here or use the upload button
          </p>
          <div>
            <input
              ref={fileInputRef}
              type="file"
              multiple
              className="hidden"
              onChange={(e) => {
                const list = e.target.files;
                if (list) {
                  for (let i = 0; i < list.length; i++) uploadMut.mutate(list[i]);
                }
                e.target.value = '';
              }}
            />
            <Button
              variant="secondary"
              size="sm"
              onClick={() => fileInputRef.current?.click()}
              disabled={uploadMut.isPending}
            >
              {uploadMut.isPending ? 'Uploading...' : 'Upload Files'}
            </Button>
          </div>
        </div>
      )}

      {uploadMut.error && (
        <p className="text-destructive text-xs mb-3">
          Upload failed: {uploadMut.error instanceof Error ? uploadMut.error.message : 'Unknown error'}
        </p>
      )}

      {!files || files.length === 0 ? (
        <EmptyState message="No files in this project." />
      ) : (
        <TableShell columns={fileColumns}>
          {files.map((f: ProjectFile) => (
            <tr key={f.path} className="border-b border-glass-edge/50 hover:bg-glass-hover/30">
              <td className="px-4 py-3">
                <span className="text-text-tertiary mr-2">{f.is_dir ? '\uD83D\uDCC1' : '\uD83D\uDCC4'}</span>
                <span className="font-mono text-sm">{f.name}</span>
              </td>
              <td className="px-4 py-3 text-text-secondary text-xs uppercase">{f.type}</td>
              <td className="px-4 py-3 text-right text-text-secondary font-mono text-xs">
                {f.is_dir ? '-' : formatFileSize(f.size)}
              </td>
              <td className="px-4 py-3 text-right">
                <div className="flex justify-end gap-1">
                  {!f.is_dir && (
                    <a
                      href={api.downloadProjectFile(projectId, f.path)}
                      className="text-text-secondary hover:text-foreground text-xs px-2 py-1 transition-colors"
                      download
                    >
                      &#8595;
                    </a>
                  )}
                  {!project.is_builtin && !f.is_dir && (
                    <button
                      onClick={() => setDeleteTarget(f.path)}
                      className="text-text-secondary hover:text-destructive text-xs px-2 py-1 transition-colors cursor-pointer"
                    >
                      &#10005;
                    </button>
                  )}
                </div>
              </td>
            </tr>
          ))}
        </TableShell>
      )}

      <ConfirmDialog
        open={!!deleteTarget}
        title="Delete File"
        message={`Delete "${deleteTarget}"? This cannot be undone.`}
        confirmLabel="Delete"
        variant="danger"
        onConfirm={() => deleteTarget && deleteMut.mutate(deleteTarget)}
        onCancel={() => setDeleteTarget(null)}
      />
    </div>
  );
}
