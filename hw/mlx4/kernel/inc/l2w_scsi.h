#pragma once

struct scsi_data_buffer {
	struct sg_table table;
	unsigned length;
};

struct scsi_device {
	unsigned int lun;
    void *host; /* local port */
    void *target; /* remote port */
};

/* SCSI command scratchpad */
struct scsi_pointer {
	char *ptr;		/* data pointer */
};

/*
 * ScsiLun: 8 byte LUN.
 */
struct scsi_lun {
	__u8 scsilun[8];
};

struct scsi_cmnd
{
	struct scsi_device *device;

	enum dma_data_direction sc_data_direction;
	unsigned short cmd_len;

    void (*scsi_done) (struct scsi_cmnd *); /* Completion function used by low-level driver */
    int result;     /* Status code from lower level driver */
	unsigned char *cmnd;
	struct scsi_data_buffer sdb;

#define SCSI_SENSE_BUFFERSIZE 	96
	unsigned char *sense_buffer;
				/* obtained by REQUEST SENSE when
				 * CHECK CONDITION is received on original
				 * command (auto-sense) */
				 
	struct scsi_pointer SCp;	/* Scratchpad used by some host adapters */
    void *srb; /* windows SRB */
    void *win_dev; /* windows device extension */
};


static inline unsigned scsi_bufflen(struct scsi_cmnd *cmd)
{
	return cmd->sdb.length;
}

static inline unsigned scsi_sg_count(struct scsi_cmnd *cmd)
{
	return cmd->sdb.table.nents;
}

static inline struct scatterlist *scsi_sglist(struct scsi_cmnd *cmd)
{
	return cmd->sdb.table.sgl;
}

/*
 * Midlevel queue return values.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056
#define SCSI_MLQUEUE_EH_RETRY    0x1057
#define SCSI_MLQUEUE_TARGET_BUSY 0x1058

/*
 * Host byte codes
 */

#define DID_OK          0x00	/* NO error                                */
#define DID_NO_CONNECT  0x01	/* Couldn't connect before timeout period  */
#define DID_BUS_BUSY    0x02	/* BUS stayed busy through time out period */
#define DID_TIME_OUT    0x03	/* TIMED OUT for other reason              */
#define DID_BAD_TARGET  0x04	/* BAD target.                             */
#define DID_ABORT       0x05	/* Told to abort for some other reason     */
#define DID_PARITY      0x06	/* Parity error                            */
#define DID_ERROR       0x07	/* Internal error                          */
#define DID_RESET       0x08	/* Reset by somebody.                      */
#define DID_BAD_INTR    0x09	/* Got an interrupt we weren't expecting.  */
#define DID_PASSTHROUGH 0x0a	/* Force command past mid-layer            */
#define DID_SOFT_ERROR  0x0b	/* The low level driver just wish a retry  */
#define DID_IMM_RETRY   0x0c	/* Retry without decrementing retry count  */
#define DID_REQUEUE	0x0d	/* Requeue command (no immediate retry) also
				 * without decrementing the retry count	   */
#define DID_TRANSPORT_DISRUPTED 0x0e /* Transport error disrupted execution
				      * and the driver blocked the port to
				      * recover the link. Transport class will
				      * retry or fail IO */
#define DID_TRANSPORT_FAILFAST	0x0f /* Transport class fastfailed the io */
#define DRIVER_OK       0x00	/* Driver status                           */

