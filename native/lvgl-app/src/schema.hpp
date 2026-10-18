#pragma once
namespace penchat {
static const char *const Schema = R"SQL(
CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
  );
CREATE TABLE IF NOT EXISTS chat_sessions (
    id TEXT PRIMARY KEY NOT NULL,
    title TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    last_message_at INTEGER NOT NULL,
    default_provider_id TEXT,
    default_model_config_id TEXT,
    message_count INTEGER NOT NULL DEFAULT 0,
    preview_text TEXT NOT NULL DEFAULT '',
    metadata_json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS chat_messages (
    id TEXT PRIMARY KEY NOT NULL,
    session_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    role TEXT NOT NULL,
    status TEXT NOT NULL,
    message_type TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    provider_id TEXT,
    model_config_id TEXT,
    request_json TEXT,
    output_text TEXT,
    reasoning_text TEXT,
    error_json TEXT,
    response_id TEXT,
    metadata_json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS message_parts (
    id TEXT PRIMARY KEY NOT NULL,
    session_id TEXT NOT NULL,
    message_id TEXT NOT NULL,
    part_index INTEGER NOT NULL,
    part_type TEXT NOT NULL,
    text TEXT,
    asset_id TEXT,
    json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS message_assets (
    id TEXT PRIMARY KEY NOT NULL,
    session_id TEXT,
    message_id TEXT,
    kind TEXT NOT NULL,
    source_type TEXT NOT NULL,
    original_path TEXT,
    stored_path TEXT,
    display_src TEXT,
    mime_type TEXT,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0,
    size_bytes INTEGER NOT NULL DEFAULT 0,
    sha256 TEXT,
    created_at INTEGER NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS stream_events (
    id TEXT PRIMARY KEY NOT NULL,
    session_id TEXT NOT NULL,
    message_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    event_type TEXT,
    event_name TEXT,
    data_json TEXT,
    raw_json TEXT,
    created_at INTEGER NOT NULL
  );
CREATE TABLE IF NOT EXISTS providers (
    id TEXT PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    base_url TEXT NOT NULL,
    api_key TEXT,
    secret_ref TEXT,
    encryption TEXT,
    headers_json TEXT NOT NULL DEFAULT '{}',
    enabled INTEGER NOT NULL DEFAULT 1,
    is_default INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS model_configs (
    id TEXT PRIMARY KEY NOT NULL,
    provider_id TEXT NOT NULL,
    name TEXT NOT NULL,
    display_name TEXT NOT NULL,
    model_type TEXT NOT NULL,
    capabilities_json TEXT NOT NULL DEFAULT '{}',
    default_parameters_json TEXT NOT NULL DEFAULT '{}',
    is_default INTEGER NOT NULL DEFAULT 0,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}'
  );
CREATE TABLE IF NOT EXISTS app_settings (
    key TEXT PRIMARY KEY NOT NULL,
    value_json TEXT NOT NULL,
    updated_at INTEGER NOT NULL
  );
PRAGMA foreign_keys = ON;
CREATE INDEX IF NOT EXISTS idx_sessions_updated ON chat_sessions(updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_messages_session_seq ON chat_messages(session_id, seq);
CREATE INDEX IF NOT EXISTS idx_parts_message ON message_parts(message_id, part_index);
CREATE INDEX IF NOT EXISTS idx_assets_session ON message_assets(session_id, created_at);
CREATE INDEX IF NOT EXISTS idx_stream_message_seq ON stream_events(message_id, seq);
)SQL";
}
