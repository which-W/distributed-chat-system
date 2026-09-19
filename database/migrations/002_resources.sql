-- Run once, with all old attachment writers and cleanup workers stopped. MySQL 8.
USE wgt;
ALTER TABLE file_transfer ADD COLUMN idempotency_key VARCHAR(128) NULL,
  ADD UNIQUE KEY uk_file_sender_idempotency (sender_uid, idempotency_key);
ALTER TABLE user ADD COLUMN avatar_id CHAR(36) NULL,
  ADD COLUMN avatar_version BIGINT UNSIGNED NOT NULL DEFAULT 0;
CREATE TABLE resource_avatar (
  id CHAR(36) PRIMARY KEY,
  owner_uid INT NOT NULL,
  variants JSON NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at TIMESTAMP NULL,
  KEY idx_avatar_expiry (expires_at),
  FOREIGN KEY (owner_uid) REFERENCES user(uid)
) ENGINE=InnoDB;
CREATE TABLE resource_outbox (
  id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  event_key VARCHAR(128) NOT NULL UNIQUE,
  kind VARCHAR(32) NOT NULL,
  payload JSON NOT NULL,
  attempts INT UNSIGNED NOT NULL DEFAULT 0,
  next_attempt_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  delivered_at TIMESTAMP NULL,
  KEY idx_resource_outbox_pending (delivered_at, next_attempt_at)
) ENGINE=InnoDB;
