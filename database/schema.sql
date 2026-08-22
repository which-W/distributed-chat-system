CREATE DATABASE IF NOT EXISTS wgt
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE wgt;

CREATE TABLE IF NOT EXISTS user_id (
  id INT NOT NULL
) ENGINE = InnoDB;

INSERT INTO user_id (id)
SELECT 1000
WHERE NOT EXISTS (SELECT 1 FROM user_id);

CREATE TABLE IF NOT EXISTS user (
  uid INT NOT NULL,
  name VARCHAR(64) NOT NULL,
  email VARCHAR(255) NOT NULL,
  pwd VARCHAR(255) NOT NULL,
  nick VARCHAR(64) NOT NULL DEFAULT '',
  `desc` VARCHAR(255) NOT NULL DEFAULT '',
  sex TINYINT NOT NULL DEFAULT 0,
  icon VARCHAR(512) NOT NULL DEFAULT '',
  PRIMARY KEY (uid),
  UNIQUE KEY uk_user_name (name),
  UNIQUE KEY uk_user_email (email)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS friend_apply (
  id BIGINT NOT NULL AUTO_INCREMENT,
  from_uid INT NOT NULL,
  to_uid INT NOT NULL,
  status TINYINT NOT NULL DEFAULT 0,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_friend_apply (from_uid, to_uid),
  KEY idx_friend_apply_to_id (to_uid, id),
  CONSTRAINT fk_friend_apply_from FOREIGN KEY (from_uid) REFERENCES user (uid),
  CONSTRAINT fk_friend_apply_to FOREIGN KEY (to_uid) REFERENCES user (uid)
) ENGINE = InnoDB;

CREATE TABLE IF NOT EXISTS friend (
  self_id INT NOT NULL,
  friend_id INT NOT NULL,
  back VARCHAR(64) NOT NULL DEFAULT '',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (self_id, friend_id),
  CONSTRAINT fk_friend_self FOREIGN KEY (self_id) REFERENCES user (uid),
  CONSTRAINT fk_friend_target FOREIGN KEY (friend_id) REFERENCES user (uid)
) ENGINE = InnoDB;

DROP PROCEDURE IF EXISTS reg_user;
DELIMITER //
CREATE PROCEDURE reg_user(
  IN p_name VARCHAR(64),
  IN p_email VARCHAR(255),
  IN p_pwd VARCHAR(255),
  OUT p_result INT
)
BEGIN
  DECLARE next_uid INT;

  IF EXISTS (SELECT 1 FROM user WHERE name = p_name OR email = p_email) THEN
    SET p_result = 0;
  ELSE
    UPDATE user_id SET id = LAST_INSERT_ID(id + 1);
    SET next_uid = LAST_INSERT_ID();
    INSERT INTO user (uid, name, email, pwd, nick)
    VALUES (next_uid, p_name, p_email, p_pwd, p_name);
    SET p_result = next_uid;
  END IF;
END //
DELIMITER ;
