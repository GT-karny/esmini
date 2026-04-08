import { useState } from 'react';
import { useMutation, useQueryClient } from '@tanstack/react-query';
import { api, type ParameterPreset } from '../../api/client';
import { TextInput } from '../ui/Input';
import { Button } from '../ui/Button';
import { ConfirmDialog } from '../ui/ConfirmDialog';

interface PresetSelectorProps {
  projectId: string;
  scenarioFile: string;
  presets: ParameterPreset[];
  currentValues: Record<string, string>;
  defaultValues: Record<string, string>;
  onLoad: (values: Record<string, string>) => void;
}

export function PresetSelector({
  projectId,
  scenarioFile,
  presets,
  currentValues,
  defaultValues,
  onLoad,
}: PresetSelectorProps) {
  const queryClient = useQueryClient();
  const [activePresetId, setActivePresetId] = useState<string | null>(null); // null = "Default"
  const [showNewInput, setShowNewInput] = useState(false);
  const [newName, setNewName] = useState('');
  const [deleteTarget, setDeleteTarget] = useState<ParameterPreset | null>(null);
  const [manageMode, setManageMode] = useState(false);

  const invalidatePresets = () =>
    queryClient.invalidateQueries({ queryKey: ['presets', projectId, scenarioFile] });

  const createMut = useMutation({
    mutationFn: () => api.createPreset(projectId, scenarioFile, newName.trim(), currentValues),
    onSuccess: (created) => {
      setShowNewInput(false);
      setNewName('');
      setActivePresetId(created.preset_id);
      invalidatePresets();
    },
  });

  const updateMut = useMutation({
    mutationFn: (presetId: string) =>
      api.updatePreset(projectId, scenarioFile, presetId, { values: currentValues }),
    onSuccess: () => invalidatePresets(),
  });

  const deleteMut = useMutation({
    mutationFn: (presetId: string) => api.deletePreset(projectId, scenarioFile, presetId),
    onSuccess: () => {
      if (deleteTarget && activePresetId === deleteTarget.preset_id) {
        setActivePresetId(null);
        onLoad(defaultValues);
      }
      setDeleteTarget(null);
      invalidatePresets();
    },
  });

  // Check if current values match a given set
  const valuesMatch = (a: Record<string, string>, b: Record<string, string>): boolean => {
    const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
    for (const k of keys) {
      if ((a[k] ?? '') !== (b[k] ?? '')) return false;
    }
    return true;
  };

  const activePreset = presets.find((p) => p.preset_id === activePresetId);
  const activeValues = activePreset?.values ?? defaultValues;
  const hasChanges = !valuesMatch(currentValues, activeValues);

  const handleSelectDefault = () => {
    setActivePresetId(null);
    onLoad(defaultValues);
  };

  const handleSelectPreset = (preset: ParameterPreset) => {
    setActivePresetId(preset.preset_id);
    onLoad({ ...defaultValues, ...preset.values });
  };

  const handleReset = () => {
    onLoad(activeValues);
  };

  const handleSaveNew = () => {
    if (newName.trim()) createMut.mutate();
  };

  return (
    <div className="mb-3">
      {/* Tabs row */}
      <div className="flex items-center gap-0.5 flex-wrap border-b border-glass-edge mb-2">
        {/* Default tab */}
        <TabButton
          active={activePresetId === null && !hasChanges}
          modified={activePresetId === null && hasChanges}
          label="Default"
          onClick={handleSelectDefault}
          manageMode={false}
        />

        {/* Preset tabs */}
        {presets.map((p) => {
          const isActive = activePresetId === p.preset_id;
          const isModified = isActive && hasChanges;
          return (
            <TabButton
              key={p.preset_id}
              active={isActive && !isModified}
              modified={isModified}
              label={p.name}
              onClick={() => handleSelectPreset(p)}
              manageMode={manageMode}
              onDelete={() => setDeleteTarget(p)}
            />
          );
        })}

        {/* Add tab */}
        {!showNewInput && (
          <button
            onClick={() => setShowNewInput(true)}
            className="px-2 py-1.5 text-[10px] text-text-tertiary hover:text-foreground hover:bg-glass-hover transition-colors cursor-pointer"
            title="Save as new preset"
          >
            +
          </button>
        )}

        {/* Manage toggle */}
        {presets.length > 0 && (
          <button
            onClick={() => setManageMode((v) => !v)}
            className={`ml-auto px-1.5 py-1 text-[9px] transition-colors cursor-pointer ${
              manageMode
                ? 'text-primary'
                : 'text-text-tertiary hover:text-text-secondary'
            }`}
            title={manageMode ? 'Done' : 'Manage presets'}
          >
            {manageMode ? 'Done' : 'Manage'}
          </button>
        )}
      </div>

      {/* New preset input */}
      {showNewInput && (
        <div className="flex items-center gap-2 mb-2">
          <TextInput
            placeholder="Preset name..."
            value={newName}
            onChange={(e) => setNewName(e.target.value)}
            className="text-xs"
            autoFocus
            onKeyDown={(e) => {
              if (e.key === 'Enter') handleSaveNew();
              if (e.key === 'Escape') { setShowNewInput(false); setNewName(''); }
            }}
          />
          <Button
            variant="primary"
            size="sm"
            disabled={!newName.trim() || createMut.isPending}
            onClick={handleSaveNew}
          >
            Save
          </Button>
          <Button
            variant="ghost"
            size="sm"
            onClick={() => { setShowNewInput(false); setNewName(''); }}
          >
            Cancel
          </Button>
        </div>
      )}

      {/* Save / Reset actions when active preset has changes */}
      {hasChanges && activePresetId && (
        <div className="flex items-center gap-2 mb-2">
          <span className="text-[10px] text-warning">Modified</span>
          <Button
            variant="ghost"
            size="sm"
            onClick={() => updateMut.mutate(activePresetId)}
            disabled={updateMut.isPending}
          >
            Save
          </Button>
          <Button variant="ghost" size="sm" onClick={handleReset}>
            Reset
          </Button>
        </div>
      )}

      {/* Delete confirmation */}
      <ConfirmDialog
        open={!!deleteTarget}
        title="Delete Preset"
        message={`Delete preset "${deleteTarget?.name}"? This cannot be undone.`}
        confirmLabel="Delete"
        variant="danger"
        onConfirm={() => deleteTarget && deleteMut.mutate(deleteTarget.preset_id)}
        onCancel={() => setDeleteTarget(null)}
      />
    </div>
  );
}

/* ------------------------------------------------------------------ */

function TabButton({
  active,
  modified,
  label,
  onClick,
  manageMode,
  onDelete,
}: {
  active: boolean;
  modified: boolean;
  label: string;
  onClick: () => void;
  manageMode: boolean;
  onDelete?: () => void;
}) {
  return (
    <div className="relative flex items-center">
      <button
        onClick={onClick}
        className={`px-2.5 py-1.5 text-[10px] font-medium transition-colors cursor-pointer border-b-2 ${
          active
            ? 'text-foreground bg-glass-active border-primary'
            : modified
              ? 'text-foreground border-warning/60'
              : 'text-text-secondary hover:text-foreground hover:bg-glass-hover border-transparent'
        }`}
      >
        {label}
        {modified && <span className="text-warning ml-0.5">*</span>}
      </button>
      {manageMode && onDelete && (
        <button
          onClick={(e) => { e.stopPropagation(); onDelete(); }}
          className="absolute -top-1 -right-1 w-3.5 h-3.5 bg-destructive/80 text-background text-[8px] leading-none flex items-center justify-center hover:bg-destructive cursor-pointer"
          title="Delete"
        >
          &times;
        </button>
      )}
    </div>
  );
}
