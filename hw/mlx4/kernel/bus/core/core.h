#pragma once

int __init ib_cache_setup(void);

void __exit ib_cache_cleanup(void);

int __init ib_core_init(void);

void __exit ib_core_cleanup(void);

void init_qp_state_tbl();

