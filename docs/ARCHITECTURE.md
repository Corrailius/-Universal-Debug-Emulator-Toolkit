# ARCHITECTURE.md
## UDET — Universal Debug & Emulator Toolkit

*Architecture-first design document. Written before the first line of production code.*
*Last updated: see git log.*

---

## Table of Contents

1. [Design Philosophy](#1-design-philosophy)
2. [System Overview](#2-system-overview)
3. [Module Breakdown](#3-module-breakdown)
4. [Plugin System](#4-plugin-system)
5. [Data Layer](#5-data-layer)
6. [UI Architecture](#6-ui-architecture)
7. [Process & Launch Model](#7-process--launch-model)
8. [Backup & Safety Model](#8-backup--safety-model)
9. [Obsidian Bridge](#9-obsidian-bridge)
10. [Cross-Cutting Concerns](#10-cross-cutting-concerns)
11. [Key Design Decisions](#11-key-design-decisions)
12. [What This Document Is Not](#12-what-this-document-is-not)

---

## 1. Design Philosophy

### Architecture first, code second

This document exists before the first `.cpp` file is written. It describes the intended structure of the system so that every implementation decision can be checked against an agreed design. When the implementation diverges from this document, one of them is wrong — and this document is updated or the code is corrected, not silently ignored.

### Four guiding constraints

These constraints shape every design decision. When two options are both reasonable, the one that better satisfies these constraints wins.

| Constraint | Meaning |
|---|---|
| **Pluggable** | Every game engine, emulator, and diagnostic rule is a plugin. Core code never hardcodes an engine name. |
| **Safe** | No file is mutated without a backup. No fix is applied without user confirmation. No data is lost silently. |
| **Dual-audience** | Every feature must work for a non-programmer using the wizard UI and for a developer using the CLI. |
| **Testable** | Business logic lives in `udet-core` and `udet-db`, which have zero Qt Widgets dependency. They can be unit-tested with Google Test without launching a GUI. |

---

## 2. System Overview

```
╔══════════════════════════════════════════════════════════════════╗
║                         udet-app                                 ║
║                                                                  ║
║   ┌─────────────────────────────────────────────────────────┐   ║
║   │                       udet-ui                           │   ║
║   │  Simple Mode (Wizards)   │   Advanced Mode (Panels)     │   ║
║   │  HubView  WizardStack    │   HexView  DecompilerPanel   │   ║
║   │  GameLibraryView         │   DiagnosticRuleEditor       │   ║
║   └────────────────────┬────────────────────────────────────┘   ║
║                        │ Qt signals / slots                      ║
║   ┌────────────────────▼────────────────────────────────────┐   ║
║   │                      udet-core                          │   ║
║   │  UniversalFile    DiagnosticEngine    FixOrchestrator   │   ║
║   │  BackupManager    ScanScheduler       PluginManager     │   ║
║   └────────────┬───────────────────┬────────────────────────┘   ║
║                │                   │                             ║
║   ┌────────────▼───────┐   ┌───────▼──────────────────────┐    ║
║   │     udet-db        │   │     udet-plugin-sdk           │    ║
║   │  LibraryModel      │   │  IEngineAdapter               │    ║
║   │  ScanCache         │   │  IDiagnosticRule              │    ║
║   │  ErrorKnowledgeBase│   │  IFixPlugin                   │    ║
║   └────────────────────┘   │  IEmulatorPlugin              │    ║
║                             └───────┬──────────────────────┘    ║
║                                     │ QPluginLoader              ║
║   ┌─────────────────────────────────▼──────────────────────┐   ║
║   │                     plugins/                            │   ║
║   │  engine-rpgmaker-xp   engine-rpgmaker-mv               │   ║
║   │  emulator-retroarch   emulator-pcsx2   emulator-mgba   │   ║
║   └────────────────────────────────────────────────────────┘   ║
╚══════════════════════════════════════════════════════════════════╝

                              ┌─────────────────────────┐
                              │       udet-cli           │
                              │  (links core + db only)  │
                              │  no Qt Widgets dep       │
                              └─────────────────────────┘
```

The UI, CLI, and all plugins are **consumers** of `udet-core`. Core has no knowledge of any of them.

---

## 3. Module Breakdown

### 3.1 `udet-core`

The heart of the application. Contains all business logic. Has **no Qt Widgets dependency** — only Qt Core (for `QString`, `QList`, etc.).

#### Key classes

| Class | Responsibility |
|---|---|
| `UniversalFile` | Abstraction over a file on disk. Carries: `QFileInfo path`, `QByteArray rawBytes`, `QVariant parsedAst`, `QVariantMap metadata`. The single object passed between the parse → diagnose → fix pipeline. |
| `PluginManager` | Discovers, loads, and caches `QPluginLoader` instances. Maintains a registry of loaded `IEngineAdapter` and `IEmulatorPlugin` instances. Queries them by file extension. |
| `DiagnosticEngine` | Accepts a `UniversalFile`. Queries `PluginManager` for the responsible `IEngineAdapter`. Calls its registered `IDiagnosticRule` set. Returns `QList<DiagnosticResult>`. |
| `FixOrchestrator` | Accepts a `DiagnosticResult`. Queries `PluginManager` for a matching `IFixPlugin`. Calls `IFixPlugin::propose()`, presents the `FixProposal` to the caller, and — only on confirmation — calls `IFixPlugin::apply()`. |
| `BackupManager` | Before any `apply()` call, `BackupManager::snapshot(path)` copies the file to a timestamped `.bak` alongside the original. Maintains a log of all backups. Provides `restore(path)`. |
| `ScanScheduler` | Manages filesystem scan jobs (initial + incremental). Runs on a `QThreadPool` worker. Emits progress signals. Results are handed to `udet-db`. |

#### Dependency rule

`udet-core` may depend on: Qt Core, `udet-plugin-sdk`, `udet-db`.
`udet-core` must **never** depend on: Qt Widgets, Qt Quick, `udet-ui`, any plugin directly.

---

### 3.2 `udet-db`

SQLite-backed persistence layer via `QtSql`. All database access goes through this module — no raw SQL anywhere else.

#### Schema (v1)

```sql
-- Game library
CREATE TABLE games (
    id          TEXT PRIMARY KEY,   -- UUID
    title       TEXT NOT NULL,
    path        TEXT NOT NULL UNIQUE,
    engine_id   TEXT,               -- matches IEngineAdapter::id()
    emulator_id TEXT,               -- matches IEmulatorPlugin::id()
    cover_path  TEXT,
    play_time   INTEGER DEFAULT 0,  -- seconds
    last_played INTEGER,            -- Unix timestamp
    added_at    INTEGER NOT NULL
);

CREATE TABLE tags (
    game_id TEXT REFERENCES games(id) ON DELETE CASCADE,
    tag     TEXT NOT NULL,
    PRIMARY KEY (game_id, tag)
);

-- Scan cache
CREATE TABLE scan_roots (
    path         TEXT PRIMARY KEY,
    last_scanned INTEGER
);

CREATE TABLE scan_entries (
    path        TEXT PRIMARY KEY,
    root        TEXT REFERENCES scan_roots(path),
    mtime       INTEGER,
    size        INTEGER,
    extension   TEXT
);

-- Error knowledge base
CREATE TABLE error_records (
    id          TEXT PRIMARY KEY,
    engine_id   TEXT,
    error_hash  TEXT UNIQUE,        -- hash of normalised error text
    error_text  TEXT,
    root_cause  TEXT,
    fix_name    TEXT,
    first_seen  INTEGER,
    seen_count  INTEGER DEFAULT 1
);
```

#### Versioning

The schema version is stored in `PRAGMA user_version`. Migrations are applied in order at startup. Each migration is a static function — no external migration files.

---

### 3.3 `udet-plugin-sdk`

Header-only (plus a small `.lib`/`.a` for Qt meta-object support). This is the public API surface for plugin authors.

```cpp
// IEngineAdapter.h
class IEngineAdapter {
public:
    virtual ~IEngineAdapter() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList handledExtensions() const = 0;

    virtual ParseResult    parse(const UniversalFile& file) const = 0;
    virtual QList<DiagnosticResult> diagnose(const UniversalFile& file) const = 0;
    virtual QStringList    launchArgs(const GameEntry& entry) const = 0;

    // Optional: return rules this adapter registers
    virtual QList<IDiagnosticRule*> rules() const { return {}; }
};
Q_DECLARE_INTERFACE(IEngineAdapter, "udet.IEngineAdapter/1.0")


// IEmulatorPlugin.h
class IEmulatorPlugin {
public:
    virtual ~IEmulatorPlugin() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList supportedExtensions() const = 0;

    virtual bool        isInstalled() const = 0;
    virtual QString     detectedPath() const = 0;
    virtual QStringList buildLaunchCommand(const GameEntry& entry) const = 0;
};
Q_DECLARE_INTERFACE(IEmulatorPlugin, "udet.IEmulatorPlugin/1.0")
```

---

### 3.4 `udet-ui`

Qt Widgets and Qt Quick / QML. Depends on `udet-core` and `udet-db`. Never calls SQLite directly.

Structure:

```
udet-ui/
├── main/
│   ├── MainWindow          Central window, toolbar, mode toggle, dock manager
│   └── ModeController      Manages Simple ↔ Advanced state, persists to QSettings
├── hub/
│   ├── GameLibraryView     QML grid/list view of the game library
│   ├── MetadataPanel       Right-side detail panel for selected game
│   └── LaunchController    Coordinates HubController → ProcessManager
├── wizards/
│   ├── OnboardingWizard    First-run: set library roots, detect emulators
│   ├── ScanWizard          Guided library scan
│   └── FixWizard           Step-by-step diagnostic + fix flow
├── advanced/
│   ├── HexViewPanel        Hex editor widget
│   ├── DecompilerPanel     Script decompiler output
│   ├── RawFileInspector    AST tree view
│   └── DiagnosticRuleEditor  View/enable/disable loaded rules
└── settings/
    └── SettingsDialog      Plugin paths, vault paths, emulator overrides
```

---

### 3.5 `udet-cli`

A standalone binary that links `udet-core` and `udet-db`. Uses `QCoreApplication`, not `QApplication`. No Qt Widgets or QML dependency.

CLI interface matches the existing Python script plus new hub commands:

```
udet <path>                        # auto-detect and open interactive menu
udet <path> --scan                 # run diagnostics
udet <path> --fix                  # run diagnostics and apply all safe fixes
udet <path> --wizard               # guided fix wizard (interactive terminal)
udet library --scan                # scan all configured library roots
udet library --list                # list all known games
udet launch <game-id>              # launch a game by its library ID
udet launch <path>                 # launch a file directly
```

---

## 4. Plugin System

### 4.1 Discovery

On startup, `PluginManager` scans:
1. `<executable-dir>/plugins/` — bundled plugins
2. `<user-data-dir>/plugins/` — user-installed plugins
3. Paths listed in `QSettings["plugin_paths"]` — developer overrides

Each `.dll` / `.so` / `.dylib` found is inspected via `QPluginLoader::metaData()`. If the `IID` matches a known interface, the plugin is loaded and registered.

### 4.2 Plugin Lifecycle

```
PluginManager::scanPluginDirs()
    │
    ├── QPluginLoader::load()          ← shared library loaded into process
    │
    ├── QPluginLoader::instance()      ← QObject* root instance created
    │
    ├── qobject_cast<IEngineAdapter*>  ← interface resolved
    │
    ├── adapter->id() verified unique  ← collision check
    │
    └── registered in adapter registry
```

Plugins are never unloaded at runtime (Qt does not support safe shared-library unload on all platforms). Plugin manager restarts require an application restart.

### 4.3 Plugin Isolation

Plugins share the process address space. They are trusted code. There is no sandboxing. The plugin guide makes this explicit: plugins are for first-party and community use, not untrusted third-party distribution.

---

## 5. Data Layer

### 5.1 LibraryModel

A `QAbstractItemModel` subclass backed by `udet-db`. Provides the game library to both the QML `GameLibraryView` (via `QAbstractItemModel` bindings) and the CLI (via direct model iteration).

Updates are append-only during a scan. Deletions (removed files) are flagged as `stale` and cleaned up at the end of a scan pass, not mid-scan.

### 5.2 Scan Flow

```
ScanScheduler::startScan(roots)
        │
        ├── worker thread: recursive QDirIterator over each root
        │
        ├── for each file matching known extension:
        │     └── compare mtime + size against scan_entries table
        │           ├── unchanged → skip
        │           └── changed / new → emit fileDiscovered(path)
        │
        ├── main thread: LibraryModel::upsertFromScan(path)
        │     └── PluginManager::adapterForExtension(ext) → IEngineAdapter*
        │           └── adapter->parse(UniversalFile(path)) → ParseResult
        │                 └── LibraryModel::update(id, metadata)
        │
        └── ScanScheduler::finalize()
              └── mark missing entries as stale → cleanup
```

### 5.3 Error Knowledge Base

The error KB is a table in SQLite, mirrored to the Obsidian project vault as individual Markdown files. The mirror is one-way: the app writes to both; Obsidian changes are not read back.

---

## 6. UI Architecture

### 6.1 Mode Toggle

```
ModeController
    │
    ├── currentMode: { Simple | Advanced }        persisted to QSettings
    │
    ├── on Simple:
    │     show: WizardStack, GameLibraryView
    │     hide: HexViewPanel, DecompilerPanel, RawFileInspector,
    │            DiagnosticRuleEditor, advanced dock widgets
    │
    └── on Advanced:
          show: all panels
          WizardStack remains accessible (tab/panel, not replaced)
```

The toggle button is always rendered in the main toolbar. It is never hidden, disabled, or moved. It is present on every screen. This is non-negotiable.

### 6.2 QML / Widgets Boundary

The game library grid view is implemented in QML for smooth scrolling, image caching, and animation. All other panels are Qt Widgets. The boundary is clean:

- `GameLibraryView.qml` communicates with `LibraryModel` via standard `QAbstractItemModel` — no C++ objects cross the boundary
- `LaunchController` (C++ / Widgets side) is exposed to QML as a `QObject` context property
- No QML file imports a plugin or calls `udet-core` directly

### 6.3 Signal / Slot Conventions

- Core emits signals; UI connects to them. Core never calls UI directly.
- Long-running operations (scan, launch, fix) run on `QThreadPool` workers. Results are marshalled back to the main thread via queued connections.
- No `QApplication::processEvents()` calls anywhere. If a UI component needs to block, it uses a `QProgressDialog` with a cancellable worker, not a busy loop.

---

## 7. Process & Launch Model

```
HubController::launch(gameId)
        │
        ├── db: GameEntry entry = LibraryModel::get(gameId)
        │
        ├── plugins: IEmulatorPlugin* emu = PluginManager::emulatorFor(entry.emulatorId)
        │
        ├── QStringList argv = emu->buildLaunchCommand(entry)
        │
        ├── BackupManager: no-op for read-only launch
        │
        ├── ProcessManager::launch(argv)
        │     └── QProcess* proc = new QProcess(parent)
        │           proc->setProgram(argv[0])
        │           proc->setArguments(argv.mid(1))
        │           proc->start()
        │
        └── LogCapture::attach(proc)
              readyReadStandardOutput → append to LogPane
              readyReadStandardError  → append to LogPane (highlighted)
              finished(exitCode) → record session end, update play_time
```

**No process is launched without the user clicking a button.** Auto-launch or background launch is not permitted without explicit setting opt-in.

---

## 8. Backup & Safety Model

### Rules

1. `BackupManager::snapshot(path)` is called by `FixOrchestrator` before every `IFixPlugin::apply()` call. This is enforced structurally — `apply()` is only callable through `FixOrchestrator`.
2. Backups are stored as `<original-filename>.<timestamp>.bak` alongside the original file.
3. The backup log is written to `udet-db` (table: `backups`).
4. `BackupManager::restore(path)` reverts the file to the most recent backup. It is accessible from both the Advanced Mode panel and the CLI (`udet restore <path>`).
5. `FixProposal` includes a `humanReadableSummary` string that is displayed to the user before `apply()` is called. Fixes are never silent.

### What is never backed up

- Files opened read-only (diagnostics, scan, inspection)
- Emulator configuration files (UDET does not write to them)
- The SQLite database itself (this is state, not user data — it is regenerated from a scan)

---

## 9. Obsidian Bridge

### Write model

`ObsidianBridge` is a service in `udet-core` with no Qt Widgets dependency. It writes plain UTF-8 Markdown files to configured paths.

```cpp
class ObsidianBridge {
public:
    void writeDevLogEntry(const DevLogEntry& entry);    // → project vault / 01 - Dev Log /
    void appendErrorKBEntry(const ErrorRecord& record); // → project vault / 02 - Error KB /
    void appendFixHistory(const FixRecord& record);     // → project vault / 04 - Fixes /
    void writeJournalStub(QDate date);                  // → personal vault / Journal /
    void writeMilestone(const Milestone& m);            // → personal vault / Milestones /
};
```

### Failure handling

If the configured vault path does not exist or is not writable, `ObsidianBridge` logs a warning and does nothing. It never throws or causes the main operation to fail. Obsidian integration is an enhancement, not a requirement.

### No reads

`ObsidianBridge` has no read methods. The vault is a write-only sink from the application's perspective.

---

## 10. Cross-Cutting Concerns

### Logging

`QLoggingCategory` with categories per module (`udet.core`, `udet.db`, `udet.plugin`, `udet.ui`). In production builds, debug categories are disabled by default. In developer builds (`CMAKE_BUILD_TYPE=Debug`), all categories are enabled. Log output goes to `stderr` in CLI mode and to the log pane + a rotating file in GUI mode.

### Error handling

- Functions that can fail return a `Result<T, Error>` type (implemented as a thin wrapper over `std::expected` or a custom type for C++20 compatibility across MSVC/Clang/GCC).
- No exception propagation across plugin boundaries. Plugins return error results; they do not throw.
- Qt signals emit error results to the UI. The UI decides how to surface them (toast, dialog, log entry).

### Threading

| Thread | Responsibilities |
|---|---|
| Main thread | All UI updates, all `QObject` interactions |
| Scan worker(s) | `QThreadPool`. Filesystem iteration, `parse()` calls |
| Launch process thread | `QProcess` stdout/stderr reading |

No raw `std::thread` is used. All concurrency goes through Qt primitives (`QThreadPool`, `QFuture`, `QProcess`).

### Internationalisation

All user-visible strings are wrapped in `tr()` from day one. The `.ts` translation files are generated via `lupdate`. No hardcoded UI strings. This is cheaper to do from the start than to retrofit.

---

## 11. Key Design Decisions

These decisions are recorded here. The reasoning behind each should also live as an ADR in the Obsidian project vault.

| Decision | Rationale |
|---|---|
| **C++ not Python** | The Python prototype validated features. C++ is chosen for performance (large library scans), cross-platform native packaging, and portfolio signal. Qt is chosen for its single-codebase cross-platform GUI and its native plugin system. |
| **Qt Plugin API over manual dlopen** | `QPluginLoader` handles platform differences (RTTI, symbol visibility) and integrates with the Qt meta-object system. Writing a custom plugin loader would replicate this work poorly. |
| **SQLite not files** | A plain-file library (JSON/YAML) does not support incremental scan updates, complex queries, or concurrent reads from CLI + GUI. SQLite adds no deployment complexity (it is bundled with Qt). |
| **Mode toggle always visible** | Hiding advanced features behind a settings page creates a "power user ghetto" and signals that the app distrusts its users. The toggle is always available. Users who want simple stay simple; users who are curious can reach advanced without hunting. |
| **Write-only Obsidian bridge** | Reading vault content at runtime would create a dependency on Obsidian's Markdown dialect and file naming conventions. Write-only is sufficient for the use case and keeps the coupling minimal. |
| **No emulator sandboxing** | UDET launches emulators as normal child processes with the emulator's own security model. Sandboxing would require platform-specific APIs and break compatibility with most existing emulators. |
| **UniversalFile not format-specific types** | A single file abstraction avoids a combinatorial explosion of adapter ↔ rule ↔ fix type relationships. Plugins receive a `UniversalFile` and interpret its `parsedAst` field according to their own schema. |

---

## 12. What This Document Is Not

This document describes **intended structure**. It does not:

- Prescribe implementation details within a module (that is the module's header/source)
- Serve as a user manual
- List all classes (only the architecturally significant ones)
- Guarantee that the current code matches this document exactly at any given commit

When this document and the code disagree, open an issue or a PR. One of them is wrong.

---

*Maintain this document as a living record. Update the relevant section when an architectural decision changes. Create an ADR in the Obsidian project vault when the change is significant.*
