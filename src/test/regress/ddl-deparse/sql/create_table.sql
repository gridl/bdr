--
-- CREATE_TABLE
--

CREATE TABLE datatype_table (
    id           SERIAL,
    id_big       BIGSERIAL,
    v_smallint   SMALLINT,
    v_int        INT,
    v_bigint     BIGINT,
    v_char       CHAR(1),
    v_varchar    VARCHAR(10),
    v_text       TEXT,
    v_bool       BOOLEAN,
    v_inet       INET,
    v_numeric    NUMERIC(1,0),
    v_float      FLOAT(1),
    v_tsvector   TSVECTOR,
    v_timestamp1 TIMESTAMP,
    v_timestamp2 TIMESTAMP WITH TIME ZONE,
    PRIMARY KEY (id),
    UNIQUE (id_big)
);

CREATE TABLE fkey_table (
    id           INT NOT NULL,
    datatype_id  INT NOT NULL REFERENCES datatype_table(id),
    big_id       BIGINT NOT NULL,
    CONSTRAINT fkey_big_id
      FOREIGN KEY (big_id)
      REFERENCES datatype_table(id_big)
);
