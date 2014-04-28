/*
 * bdr.h
 *
 * BiDirectionalReplication
 *
 * Copyright (c) 2012-2013, PostgreSQL Global Development Group
 *
 * contrib/bdr/bdr.h
 */
#ifndef BDR_H
#define BDR_H

#include "access/xlogdefs.h"
#include "libpq-fe.h"
#include "postmaster/bgworker.h"
#include "replication/logical.h"
#include "utils/resowner.h"

#define BDR_VERSION_NUM 500
#define BDR_SLOT_NAME_FORMAT "bdr_%u_%s_%u_%u__%s"
#define BDR_NODE_ID_FORMAT "bdr_"UINT64_FORMAT"_%u_%u_%u_%s"

#define BDR_INIT_REPLICA_CMD "bdr_initial_load"

/*
 * Flags to indicate which fields are present in a commit record sent by the
 * output plugin.
 */
typedef enum BdrOutputCommitFlags
{
	BDR_OUTPUT_COMMIT_HAS_ORIGIN = 1
} BdrOutputCommitFlags;

/*
 * BdrApplyWorker describes a BDR worker connection.
 *
 * This struct is stored in an array in shared memory, so it can't have any
 * pointers.
 */
typedef struct BdrApplyWorker
{
	/* local & remote database name */
	NameData dbname;

	/* connection name specified in configuration */
	NameData name;

	/* TODO: Remove these from shm, into bdr worker global state */
	RepNodeId origin_id;
	uint64 sysid;
	TimeLineID timeline;

	/*
	 * If not InvalidXLogRecPtr, stop replay at this point and exit.
	 *
	 * To save shmem space in apply workers, this is reset to InvalidXLogRecPtr
	 * if replay is successfully completed instead of setting a separate flag.
	 */
	XLogRecPtr replay_stop_lsn;

	/* Request that the remote forward all changes from other nodes */
	bool forward_changesets;

} BdrApplyWorker;

/*
 * BDRPerdbCon describes a per-database worker, a static bgworker that manages
 * BDR for a given DB.
 */
typedef struct BdrPerdbWorker
{
	/* local & remote database name */
	NameData dbname;

	size_t seq_slot;

} BdrPerdbWorker;


/*
 * Type of BDR worker in a BdrWorker struct
 */
typedef enum
{
	/*
	 * This shm array slot is unused and may be allocated. Must be zero,
	 * as it's set by memset(...) during shm segment init.
	 */
	BDR_WORKER_EMPTY_SLOT = 0,
	/* This shm array slot contains data for a */
	BDR_WORKER_APPLY,
	/* This is data for a per-database worker BdrPerdbWorker */
	BDR_WORKER_PERDB
} BdrWorkerType;

/*
 * BDRWorker entries describe shared memory slots that keep track of
 * all BDR worker types. A slot may contain data for a number of different
 * kinds of worker; this union makes sure each slot is the same size and
 * is easily accessed via an array.
 */
typedef struct BdrWorker
{
	/* Type of worker. Also used to determine if this shm slot is free. */
	BdrWorkerType worker_type;

	union worker_data {
		BdrApplyWorker apply_worker;
		BdrPerdbWorker perdb_worker;
	} worker_data;

} BdrWorker;

/* GUCs */
extern int	bdr_default_apply_delay;
extern int bdr_max_workers;
extern char *bdr_temp_dump_directory;

/*
 * Header for the shared memory segment ref'd by the BdrWorkerCtl ptr,
 * containing bdr_max_workers entries of BdrWorkerCon .
 */
typedef struct BdrWorkerControl
{
	/* Must hold this lock when writing to BdrWorkerControl members */
	LWLockId     lock;
	/* Required only for bgworker restart issues: */
	bool		 launch_workers;
	/* Set/unset by bdr_apply_pause()/_replay(). */
	bool		 pause_apply;
	/* Array members, of size bdr_max_workers */
	BdrWorker    slots[FLEXIBLE_ARRAY_MEMBER];
} BdrWorkerControl;

extern BdrWorkerControl *BdrWorkerCtl;

extern ResourceOwner bdr_saved_resowner;

/* bdr_nodes table oid */
extern Oid	BdrNodesRelid;

/* DDL replication support */
extern Oid	QueuedDDLCommandsRelid;
extern Oid	QueuedDropsRelid;

/* sequencer support */
extern Oid	BdrSequenceValuesRelid;
extern Oid	BdrSequenceElectionsRelid;
extern Oid	BdrVotesRelid;

/* Helpers for accessing configuration */
const char *bdr_get_worker_option(const char * worker_name, const char * option_name, bool missing_ok);

/* apply support */
extern void process_remote_begin(StringInfo s);
extern bool process_remote_commit(StringInfo s);
extern void process_remote_insert(StringInfo s);
extern void process_remote_update(StringInfo s);
extern void process_remote_delete(StringInfo s);

/* sequence support */
extern void bdr_sequencer_shmem_init(int nnodes, int sequencers);
extern void bdr_sequencer_init(int seq_slot);
extern void bdr_sequencer_vote(void);
extern void bdr_sequencer_tally(void);
extern void bdr_sequencer_start_elections(void);
extern void bdr_sequencer_fill_sequences(void);
extern int  bdr_node_count(void);

extern void bdr_sequencer_wakeup(void);
extern void bdr_schedule_eoxact_sequencer_wakeup(void);

extern void bdr_sequence_alloc(PG_FUNCTION_ARGS);
extern void bdr_sequence_setval(PG_FUNCTION_ARGS);
extern Datum bdr_sequence_options(PG_FUNCTION_ARGS);

/* statistic functions */
extern void bdr_count_shmem_init(size_t nnodes);
extern void bdr_count_set_current_node(RepNodeId node_id);
extern void bdr_count_commit(void);
extern void bdr_count_rollback(void);
extern void bdr_count_insert(void);
extern void bdr_count_insert_conflict(void);
extern void bdr_count_update(void);
extern void bdr_count_update_conflict(void);
extern void bdr_count_delete(void);
extern void bdr_count_delete_conflict(void);
extern void bdr_count_disconnect(void);

/* compat check functions */
extern bool bdr_get_float4byval(void);
extern bool bdr_get_float8byval(void);
extern bool bdr_get_integer_timestamps(void);
extern bool bdr_get_bigendian(void);

/* initialize a new bdr member */
extern void bdr_init_replica(Name dbname);

/* shared memory management */
extern BdrWorker* bdr_worker_shmem_alloc(BdrWorkerType worker_type);
extern void bdr_worker_shmem_release(BdrWorker* worker, BackgroundWorkerHandle *handle);

/* forbid commands we do not support currently (or never will) */
extern void init_bdr_commandfilter(void);

/* background workers */
extern void bdr_apply_main(Datum main_arg);

/* helpers shared by multiple worker types */
extern PGconn*
bdr_connect(char *conninfo_repl,
			char* remote_ident, size_t remote_ident_length,
			NameData* slot_name,
			uint64* remote_sysid_i, TimeLineID *remote_tlid_i);

extern PGconn*
bdr_establish_connection_and_slot(Name connection_name, Name out_slot_name,
	uint64 *out_sysid, TimeLineID* out_timeline, RepNodeId
	*out_replication_identifier, char **out_snapshot);

#endif	/* BDR_H */