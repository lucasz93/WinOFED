#ifndef _MLX4_EN_PORT_H_
#define _MLX4_EN_PORT_H_


#define MAC_VALID_SHIFT		63
#define VLAN_VALID_SHIFT	31
#define VLAN_MASK		0xfff
#define QUERY_PORT_ETH_MASK	1 << 1
#define QUERY_PORT_LINK_MASK	0x2 << 4
#define SET_PORT_GEN_ALL_VALID	0x7

enum {
	MCAST_DIRECT_ONLY	= 0,
	MCAST_DIRECT		= 1,
	MCAST_DEFAULT		= 2
};

enum mlx4_special_vlan_idx {
	MLX4_NO_VLAN_IDX        = 0,
	MLX4_VLAN_MISS_IDX,
	MLX4_VLAN_REGULAR
};

enum {
	MLX4_CMD_SET_VLAN_FLTR  = 0x47,
	MLX4_CMD_SET_MCAST_FLTR = 0x48,
	MLX4_CMD_DUMP_ETH_STATS = 0x49,
};

enum {
	MLX4_MCAST_CONFIG       = 0,
	MLX4_MCAST_DISABLE      = 1,
	MLX4_MCAST_ENABLE       = 2,
};

struct mlx4_set_mcast_fltr_mbox {
	__be64 mac;
};

struct mlx4_en_stat_out_mbox {
	/* Received frames with a length of 64 octets */
	__be64 R64_prio_0;
	__be64 R64_prio_1;
	__be64 R64_prio_2;
	__be64 R64_prio_3;
	__be64 R64_prio_4;
	__be64 R64_prio_5;
	__be64 R64_prio_6;
	__be64 R64_prio_7;
	__be64 R64_novlan;
	/* Received frames with a length of 127 octets */
	__be64 R127_prio_0;
	__be64 R127_prio_1;
	__be64 R127_prio_2;
	__be64 R127_prio_3;
	__be64 R127_prio_4;
	__be64 R127_prio_5;
	__be64 R127_prio_6;
	__be64 R127_prio_7;
	__be64 R127_novlan;
	/* Received frames with a length of 255 octets */
	__be64 R255_prio_0;
	__be64 R255_prio_1;
	__be64 R255_prio_2;
	__be64 R255_prio_3;
	__be64 R255_prio_4;
	__be64 R255_prio_5;
	__be64 R255_prio_6;
	__be64 R255_prio_7;
	__be64 R255_novlan;
	/* Received frames with a length of 511 octets */
	__be64 R511_prio_0;
	__be64 R511_prio_1;
	__be64 R511_prio_2;
	__be64 R511_prio_3;
	__be64 R511_prio_4;
	__be64 R511_prio_5;
	__be64 R511_prio_6;
	__be64 R511_prio_7;
	__be64 R511_novlan;
	/* Received frames with a length of 1023 octets */
	__be64 R1023_prio_0;
	__be64 R1023_prio_1;
	__be64 R1023_prio_2;
	__be64 R1023_prio_3;
	__be64 R1023_prio_4;
	__be64 R1023_prio_5;
	__be64 R1023_prio_6;
	__be64 R1023_prio_7;
	__be64 R1023_novlan;
	/* Received frames with a length of 1518 octets */
	__be64 R1518_prio_0;
	__be64 R1518_prio_1;
	__be64 R1518_prio_2;
	__be64 R1518_prio_3;
	__be64 R1518_prio_4;
	__be64 R1518_prio_5;
	__be64 R1518_prio_6;
	__be64 R1518_prio_7;
	__be64 R1518_novlan;
	/* Received frames with a length of 1522 octets */
	__be64 R1522_prio_0;
	__be64 R1522_prio_1;
	__be64 R1522_prio_2;
	__be64 R1522_prio_3;
	__be64 R1522_prio_4;
	__be64 R1522_prio_5;
	__be64 R1522_prio_6;
	__be64 R1522_prio_7;
	__be64 R1522_novlan;
	/* Received frames with a length of 1548 octets */
	__be64 R1548_prio_0;
	__be64 R1548_prio_1;
	__be64 R1548_prio_2;
	__be64 R1548_prio_3;
	__be64 R1548_prio_4;
	__be64 R1548_prio_5;
	__be64 R1548_prio_6;
	__be64 R1548_prio_7;
	__be64 R1548_novlan;
	/* Received frames with a length of 1548 < octets < MTU */
	__be64 R2MTU_prio_0;
	__be64 R2MTU_prio_1;
	__be64 R2MTU_prio_2;
	__be64 R2MTU_prio_3;
	__be64 R2MTU_prio_4;
	__be64 R2MTU_prio_5;
	__be64 R2MTU_prio_6;
	__be64 R2MTU_prio_7;
	__be64 R2MTU_novlan;
	/* Received frames with a length of MTU< octets and good CRC */
	__be64 RGIANT_prio_0;
	__be64 RGIANT_prio_1;
	__be64 RGIANT_prio_2;
	__be64 RGIANT_prio_3;
	__be64 RGIANT_prio_4;
	__be64 RGIANT_prio_5;
	__be64 RGIANT_prio_6;
	__be64 RGIANT_prio_7;
	__be64 RGIANT_novlan;
	/* Received broadcast frames with good CRC */
	__be64 RBCAST_prio_0;
	__be64 RBCAST_prio_1;
	__be64 RBCAST_prio_2;
	__be64 RBCAST_prio_3;
	__be64 RBCAST_prio_4;
	__be64 RBCAST_prio_5;
	__be64 RBCAST_prio_6;
	__be64 RBCAST_prio_7;
	__be64 RBCAST_novlan;
	/* Received multicast frames with good CRC */
	__be64 MCAST_prio_0;
	__be64 MCAST_prio_1;
	__be64 MCAST_prio_2;
	__be64 MCAST_prio_3;
	__be64 MCAST_prio_4;
	__be64 MCAST_prio_5;
	__be64 MCAST_prio_6;
	__be64 MCAST_prio_7;
	__be64 MCAST_novlan;
	/* Received unicast not short or GIANT frames with good CRC */
	__be64 RTOTG_prio_0;
	__be64 RTOTG_prio_1;
	__be64 RTOTG_prio_2;
	__be64 RTOTG_prio_3;
	__be64 RTOTG_prio_4;
	__be64 RTOTG_prio_5;
	__be64 RTOTG_prio_6;
	__be64 RTOTG_prio_7;
	__be64 RTOTG_novlan;

	/* Count of total octets of received frames, includes framing characters */
	__be64 RTTLOCT_prio_0;
	/* Count of total octets of received frames, not including framing
	   characters */
	__be64 RTTLOCT_NOFRM_prio_0;
	/* Count of Total number of octets received
	   (only for frames without errors) */
	__be64 ROCT_prio_0;

	__be64 RTTLOCT_prio_1;
	__be64 RTTLOCT_NOFRM_prio_1;
	__be64 ROCT_prio_1;

	__be64 RTTLOCT_prio_2;
	__be64 RTTLOCT_NOFRM_prio_2;
	__be64 ROCT_prio_2;

	__be64 RTTLOCT_prio_3;
	__be64 RTTLOCT_NOFRM_prio_3;
	__be64 ROCT_prio_3;

	__be64 RTTLOCT_prio_4;
	__be64 RTTLOCT_NOFRM_prio_4;
	__be64 ROCT_prio_4;

	__be64 RTTLOCT_prio_5;
	__be64 RTTLOCT_NOFRM_prio_5;
	__be64 ROCT_prio_5;

	__be64 RTTLOCT_prio_6;
	__be64 RTTLOCT_NOFRM_prio_6;
	__be64 ROCT_prio_6;

	__be64 RTTLOCT_prio_7;
	__be64 RTTLOCT_NOFRM_prio_7;
	__be64 ROCT_prio_7;

	__be64 RTTLOCT_novlan;
	__be64 RTTLOCT_NOFRM_novlan;
	__be64 ROCT_novlan;

	/* Count of Total received frames including bad frames */
	__be64 RTOT_prio_0;
	/* Count of  Total number of received frames with 802.1Q encapsulation */
	__be64 R1Q_prio_0;
	__be64 reserved1;

	__be64 RTOT_prio_1;
	__be64 R1Q_prio_1;
	__be64 reserved2;

	__be64 RTOT_prio_2;
	__be64 R1Q_prio_2;
	__be64 reserved3;

	__be64 RTOT_prio_3;
	__be64 R1Q_prio_3;
	__be64 reserved4;

	__be64 RTOT_prio_4;
	__be64 R1Q_prio_4;
	__be64 reserved5;

	__be64 RTOT_prio_5;
	__be64 R1Q_prio_5;
	__be64 reserved6;

	__be64 RTOT_prio_6;
	__be64 R1Q_prio_6;
	__be64 reserved7;

	__be64 RTOT_prio_7;
	__be64 R1Q_prio_7;
	__be64 reserved8;

	__be64 RTOT_novlan;
	__be64 R1Q_novlan;
	__be64 reserved9;

	/* Total number of Successfully Received Control Frames */
	__be64 RCNTL;
	__be64 reserved10;
	__be64 reserved11;
	__be64 reserved12;
	/* Count of received frames with a length/type field  value between 46
	   (42 for VLANtagged frames) and 1500 (also 1500 for VLAN-tagged frames),
	   inclusive */
	__be64 RInRangeLengthErr;
	/* Count of received frames with length/type field between 1501 and 1535
	   decimal, inclusive */
	__be64 ROutRangeLengthErr;
	/* Count of received frames that are longer than max allowed size for
	   802.3 frames (1518/1522) */
	__be64 RFrmTooLong;
	/* Count frames received with PCS error */
	__be64 PCS;

	/* Transmit frames with a length of 64 octets */
	__be64 T64_prio_0;
	__be64 T64_prio_1;
	__be64 T64_prio_2;
	__be64 T64_prio_3;
	__be64 T64_prio_4;
	__be64 T64_prio_5;
	__be64 T64_prio_6;
	__be64 T64_prio_7;
	__be64 T64_novlan;
	__be64 T64_loopbk;
	/* Transmit frames with a length of 65 to 127 octets. */
	__be64 T127_prio_0;
	__be64 T127_prio_1;
	__be64 T127_prio_2;
	__be64 T127_prio_3;
	__be64 T127_prio_4;
	__be64 T127_prio_5;
	__be64 T127_prio_6;
	__be64 T127_prio_7;
	__be64 T127_novlan;
	__be64 T127_loopbk;
	/* Transmit frames with a length of 128 to 255 octets */
	__be64 T255_prio_0;
	__be64 T255_prio_1;
	__be64 T255_prio_2;
	__be64 T255_prio_3;
	__be64 T255_prio_4;
	__be64 T255_prio_5;
	__be64 T255_prio_6;
	__be64 T255_prio_7;
	__be64 T255_novlan;
	__be64 T255_loopbk;
	/* Transmit frames with a length of 256 to 511 octets */
	__be64 T511_prio_0;
	__be64 T511_prio_1;
	__be64 T511_prio_2;
	__be64 T511_prio_3;
	__be64 T511_prio_4;
	__be64 T511_prio_5;
	__be64 T511_prio_6;
	__be64 T511_prio_7;
	__be64 T511_novlan;
	__be64 T511_loopbk;
	/* Transmit frames with a length of 512 to 1023 octets */
	__be64 T1023_prio_0;
	__be64 T1023_prio_1;
	__be64 T1023_prio_2;
	__be64 T1023_prio_3;
	__be64 T1023_prio_4;
	__be64 T1023_prio_5;
	__be64 T1023_prio_6;
	__be64 T1023_prio_7;
	__be64 T1023_novlan;
	__be64 T1023_loopbk;
	/* Transmit frames with a length of 1024 to 1518 octets */
	__be64 T1518_prio_0;
	__be64 T1518_prio_1;
	__be64 T1518_prio_2;
	__be64 T1518_prio_3;
	__be64 T1518_prio_4;
	__be64 T1518_prio_5;
	__be64 T1518_prio_6;
	__be64 T1518_prio_7;
	__be64 T1518_novlan;
	__be64 T1518_loopbk;
	/* Counts transmit frames with a length of 1519 to 1522 bytes */
	__be64 T1522_prio_0;
	__be64 T1522_prio_1;
	__be64 T1522_prio_2;
	__be64 T1522_prio_3;
	__be64 T1522_prio_4;
	__be64 T1522_prio_5;
	__be64 T1522_prio_6;
	__be64 T1522_prio_7;
	__be64 T1522_novlan;
	__be64 T1522_loopbk;
	/* Transmit frames with a length of 1523 to 1548 octets */
	__be64 T1548_prio_0;
	__be64 T1548_prio_1;
	__be64 T1548_prio_2;
	__be64 T1548_prio_3;
	__be64 T1548_prio_4;
	__be64 T1548_prio_5;
	__be64 T1548_prio_6;
	__be64 T1548_prio_7;
	__be64 T1548_novlan;
	__be64 T1548_loopbk;
	/* Counts transmit frames with a length of 1549 to MTU bytes */
	__be64 T2MTU_prio_0;
	__be64 T2MTU_prio_1;
	__be64 T2MTU_prio_2;
	__be64 T2MTU_prio_3;
	__be64 T2MTU_prio_4;
	__be64 T2MTU_prio_5;
	__be64 T2MTU_prio_6;
	__be64 T2MTU_prio_7;
	__be64 T2MTU_novlan;
	__be64 T2MTU_loopbk;
	/* Transmit frames with a length greater than MTU octets and a good CRC. */
	__be64 TGIANT_prio_0;
	__be64 TGIANT_prio_1;
	__be64 TGIANT_prio_2;
	__be64 TGIANT_prio_3;
	__be64 TGIANT_prio_4;
	__be64 TGIANT_prio_5;
	__be64 TGIANT_prio_6;
	__be64 TGIANT_prio_7;
	__be64 TGIANT_novlan;
	__be64 TGIANT_loopbk;
	/* Transmit broadcast frames with a good CRC */
	__be64 TBCAST_prio_0;
	__be64 TBCAST_prio_1;
	__be64 TBCAST_prio_2;
	__be64 TBCAST_prio_3;
	__be64 TBCAST_prio_4;
	__be64 TBCAST_prio_5;
	__be64 TBCAST_prio_6;
	__be64 TBCAST_prio_7;
	__be64 TBCAST_novlan;
	__be64 TBCAST_loopbk;
	/* Transmit multicast frames with a good CRC */
	__be64 TMCAST_prio_0;
	__be64 TMCAST_prio_1;
	__be64 TMCAST_prio_2;
	__be64 TMCAST_prio_3;
	__be64 TMCAST_prio_4;
	__be64 TMCAST_prio_5;
	__be64 TMCAST_prio_6;
	__be64 TMCAST_prio_7;
	__be64 TMCAST_novlan;
	__be64 TMCAST_loopbk;
	/* Transmit good frames that are neither broadcast nor multicast */
	__be64 TTOTG_prio_0;
	__be64 TTOTG_prio_1;
	__be64 TTOTG_prio_2;
	__be64 TTOTG_prio_3;
	__be64 TTOTG_prio_4;
	__be64 TTOTG_prio_5;
	__be64 TTOTG_prio_6;
	__be64 TTOTG_prio_7;
	__be64 TTOTG_novlan;
	__be64 TTOTG_loopbk;

	/* total octets of transmitted frames, including framing characters */
	__be64 TTTLOCT_prio_0;
	/* total octets of transmitted frames, not including framing characters */
	__be64 TTTLOCT_NOFRM_prio_0;
	/* ifOutOctets */
	__be64 TOCT_prio_0;

	__be64 TTTLOCT_prio_1;
	__be64 TTTLOCT_NOFRM_prio_1;
	__be64 TOCT_prio_1;

	__be64 TTTLOCT_prio_2;
	__be64 TTTLOCT_NOFRM_prio_2;
	__be64 TOCT_prio_2;

	__be64 TTTLOCT_prio_3;
	__be64 TTTLOCT_NOFRM_prio_3;
	__be64 TOCT_prio_3;

	__be64 TTTLOCT_prio_4;
	__be64 TTTLOCT_NOFRM_prio_4;
	__be64 TOCT_prio_4;

	__be64 TTTLOCT_prio_5;
	__be64 TTTLOCT_NOFRM_prio_5;
	__be64 TOCT_prio_5;

	__be64 TTTLOCT_prio_6;
	__be64 TTTLOCT_NOFRM_prio_6;
	__be64 TOCT_prio_6;

	__be64 TTTLOCT_prio_7;
	__be64 TTTLOCT_NOFRM_prio_7;
	__be64 TOCT_prio_7;

	__be64 TTTLOCT_novlan;
	__be64 TTTLOCT_NOFRM_novlan;
	__be64 TOCT_novlan;

	__be64 TTTLOCT_loopbk;
	__be64 TTTLOCT_NOFRM_loopbk;
	__be64 TOCT_loopbk;

	/* Total frames transmitted with a good CRC that are not aborted  */
	__be64 TTOT_prio_0;
	/* Total number of frames transmitted with 802.1Q encapsulation */
	__be64 T1Q_prio_0;
	__be64 reserved13;

	__be64 TTOT_prio_1;
	__be64 T1Q_prio_1;
	__be64 reserved14;

	__be64 TTOT_prio_2;
	__be64 T1Q_prio_2;
	__be64 reserved15;

	__be64 TTOT_prio_3;
	__be64 T1Q_prio_3;
	__be64 reserved16;

	__be64 TTOT_prio_4;
	__be64 T1Q_prio_4;
	__be64 reserved17;

	__be64 TTOT_prio_5;
	__be64 T1Q_prio_5;
	__be64 reserved18;

	__be64 TTOT_prio_6;
	__be64 T1Q_prio_6;
	__be64 reserved19;

	__be64 TTOT_prio_7;
	__be64 T1Q_prio_7;
	__be64 reserved20;

	__be64 TTOT_novlan;
	__be64 T1Q_novlan;
	__be64 reserved21;

	__be64 TTOT_loopbk;
	__be64 T1Q_loopbk;
	__be64 reserved22;

	/* Received frames with a length greater than MTU octets and a bad CRC */
	__be32 RJBBR;
	/* Received frames with a bad CRC that are not runts, jabbers,
	   or alignment errors */
	__be32 RCRC;
	/* Received frames with SFD with a length of less than 64 octets and a
	   bad CRC */
	__be32 RRUNT;
	/* Received frames with a length less than 64 octets and a good CRC */
	__be32 RSHORT;
	/* Total Number of Received Packets Dropped */
	__be32 RDROP;
	/* Drop due to overflow  */
	__be32 RdropOvflw;
	/* Drop due to overflow */
	__be32 RdropLength;
	/* Total of good frames. Does not include frames received with
	   frame-too-long, FCS, or length errors */
	__be32 RTOTFRMS;
	/* Total dropped Xmited packets */
	__be32 TDROP;
};


#endif

