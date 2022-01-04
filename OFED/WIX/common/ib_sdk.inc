<Include>

  <!-- Copy header files(.h) and samples\ to %SystemDrive%\OFED_SDK -->

  <Directory Id="OFED_SDK" Name="OFED_SDK">

    <Directory Id="IB_libs" Name="lib">
      <Component Id="cIBAL_Libs"
                 Guid="12E15947-F84E-49d0-B5DF-FFC505C3F5C9">
        <CreateFolder />
        <File Id="ibal.lib" DiskId="1"
              Name="ibal.lib"
              Source="..\bin\HCA\$(var.ARCH)\ibal.lib" />
        <File Id="ibal.pdb" DiskId="1"
              Name="ibal.pdb"
              Source="..\bin\HCA\$(var.ARCH)\ibal.pdb" />
        <File Id="complib.lib" DiskId="1"
              Name="complib.lib"
              Source="..\bin\HCA\$(var.ARCH)\complib.lib" />
        <File Id="complib.pdb" DiskId="1"
              Name="complib.pdb"
              Source="..\bin\HCA\$(var.ARCH)\complib.pdb" />

         <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
             <!-- SysWOW64\ 32-bit libs -->
            <File Id="cl32.lib" DiskId="1"
                  Name="complib32.lib"
                  Source="..\bin\HCA\$(var.ARCH)\cl32.lib" />
            <File Id="cl32.pdb" DiskId="1"
                  Name="complib32.pdb"
                  Source="..\bin\HCA\$(var.ARCH)\cl32.pdb" />
            <File Id="ibal32.lib" DiskId="1"
                  Name="ibal32.lib"
                  Source="..\bin\HCA\$(var.ARCH)\ibal32.lib" />
            <File Id="ibal32.pdb" DiskId="1"
                  Name="ibal32.pdb"
                  Source="..\bin\HCA\$(var.ARCH)\ibal32.pdb" />
        <?endif?>

        <File Id="winverbs.lib" DiskId="1"
              Name="winverbs.lib"
              Source="..\bin\HCA\$(var.ARCH)\winverbs.lib" />
        <File Id="winverbs.pdb" DiskId="1"
              Name="winverbs.pdb"
              Source="..\bin\HCA\$(var.ARCH)\winverbs.pdb" />

        <File Id="libibverbs.lib" DiskId="1"
              Name="libibverbs.lib"
              Source="..\bin\HCA\$(var.ARCH)\libibverbs.lib" />
        <File Id="libibverbs2.pdb" DiskId="1"
              Name="libibverbs.pdb"
              Source="..\bin\HCA\$(var.ARCH)\libibverbs.pdb" />

        <File Id="_librdmacm.lib" DiskId="1"
              Name="librdmacm.lib"
              Source="..\bin\HCA\$(var.ARCH)\librdmacm.lib" />
        <File Id="_librdmacm.pdb" DiskId="1"
              Name="librdmacm.pdb"
              Source="..\bin\HCA\$(var.ARCH)\librdmacm.pdb" />

        <File Id="libibmad.lib" DiskId="1"
              Name="libibmad.lib"
              Source="..\bin\HCA\$(var.ARCH)\libibmad.lib" />
        <File Id="libibmad2.pdb" DiskId="1"
              Name="libibmad.pdb"
              Source="..\bin\HCA\$(var.ARCH)\libibmad.pdb" />

        <File Id="libibumad.lib" DiskId="1"
              Name="libibumad.lib"
              Source="..\bin\HCA\$(var.ARCH)\libibumad.lib" />
        <File Id="libibumad2.pdb" DiskId="1"
              Name="libibumad.pdb"
              Source="..\bin\HCA\$(var.ARCH)\libibumad.pdb" />
      </Component>
    </Directory>

    <Directory Id="IB_inc" Name="Inc">
      <Component Id="cIBAL_Incs"
                 Guid="54C2903E-65DF-4c62-B31D-21DB1AFB5819">
        <CreateFolder />

        <File Id="oib_ver.h" DiskId="1"
               Name="oib_ver.h"
               Source="..\bin\Inc\oib_ver.h" />
        <File Id="mod_ver.def" DiskId="1"
               Name="mod_ver.def"
               Source="..\bin\Inc\mod_ver.def" />
        <File Id="openib.def" DiskId="1"
               Name="openib.def"
               Source="..\bin\Inc\openib.def" />
        <File Id="comp_channel.h" DiskId="1"
               Name="comp_channel.h"
               Source="..\bin\Inc\comp_channel.h" />
        <File Id="dlist.h" DiskId="1"
               Name="dlist.h"
               Source="..\bin\Inc\dlist.h" />
        <File Id="getopt.h" DiskId="1"
               Name="getopt.h"
               Source="..\bin\Inc\getopt.h" />
        <File Id="get_clock.h" DiskId="1"
               Name="get_clock.h"
               Source="..\..\..\..\tools\perftests\user\get_clock.h" />
      </Component>

      <Directory Id="IB_complib" Name="complib">
        <Component Id="cIBAL_Complib"
                   Guid="9F9EE1F2-5ACE-47c7-89F8-B0EA1D4363AF">
          <CreateFolder />
          <File Id="cl_async_proc.h" DiskId="1"
                Name="cl_async_proc.h"
                Source="..\bin\Inc\complib\cl_async_proc.h" />
          <File Id="cl_atomic.h" DiskId="1"
                Name="cl_atomic.h"
                Source="..\bin\Inc\complib\cl_atomic.h" />
          <File Id="cl_atomic_osd.h" DiskId="1"
                Name="cl_atomic_osd.h"
                Source="..\bin\Inc\complib\cl_atomic_osd.h" />
          <File Id="cl_byteswap.h" DiskId="1"
                Name="cl_byteswap.h"
                Source="..\bin\Inc\complib\cl_byteswap.h" />
          <File Id="cl_byteswap_osd.h" DiskId="1"
                Name="cl_byteswap_osd.h"
                Source="..\bin\Inc\complib\cl_byteswap_osd.h" />
          <File Id="cl_comppool.h" DiskId="1"
                Name="cl_comppool.h"
                Source="..\bin\Inc\complib\cl_comppool.h" />
          <File Id="cl_debug.h" DiskId="1"
                Name="cl_debug.h"
                Source="..\bin\Inc\complib\cl_debug.h" />
          <File Id="cl_debug_osd.h" DiskId="1"
                Name="cl_debug_osd.h"
                Source="..\bin\Inc\complib\cl_debug_osd.h" />
          <File Id="cl_event.h" DiskId="1"
                Name="cl_event.h"
                Source="..\bin\Inc\complib\cl_event.h" />
          <File Id="cl_event_osd.h" DiskId="1"
                Name="cl_event_osd.h"
                Source="..\bin\Inc\complib\cl_event_osd.h" />
          <File Id="cl_fleximap.h" DiskId="1"
                Name="cl_fleximap.h"
                Source="..\bin\Inc\complib\cl_fleximap.h" />
          <File Id="cl_ioctl.h" DiskId="1"
                Name="cl_ioctl.h"
                Source="..\bin\Inc\complib\cl_ioctl.h" />
          <File Id="cl_ioctl_osd.h" DiskId="1"
                Name="cl_ioctl_osd.h"
                Source="..\bin\Inc\complib\cl_ioctl_osd.h" />
          <File Id="cl_irqlock.h" DiskId="1"
                Name="cl_irqlock.h"
                Source="..\bin\Inc\complib\cl_irqlock.h" />
          <File Id="cl_list.h" DiskId="1"
                Name="cl_list.h"
                Source="..\bin\Inc\complib\cl_list.h" />
          <File Id="cl_log.h" DiskId="1"
                Name="cl_log.h"
                Source="..\bin\Inc\complib\cl_log.h" />
          <File Id="cl_map.h" DiskId="1"
                Name="cl_map.h"
                Source="..\bin\Inc\complib\cl_map.h" />
          <File Id="cl_math.h" DiskId="1"
                Name="cl_math.h"
                Source="..\bin\Inc\complib\cl_math.h" />
          <File Id="cl_memory.h" DiskId="1"
                Name="cl_memory.h"
                Source="..\bin\Inc\complib\cl_memory.h" />
          <File Id="cl_memory_osd.h" DiskId="1"
                Name="cl_memory_osd.h"
                Source="..\bin\Inc\complib\cl_memory_osd.h" />
          <File Id="cl_mutex.h" DiskId="1"
                Name="cl_mutex.h"
                Source="..\bin\Inc\complib\cl_mutex.h" />
          <File Id="cl_mutex_osd.h" DiskId="1"
                Name="cl_mutex_osd.h"
                Source="..\bin\Inc\complib\cl_mutex_osd.h" />
          <File Id="cl_obj.h" DiskId="1"
                Name="cl_obj.h"
                Source="..\bin\Inc\complib\cl_obj.h" />
          <File Id="cl_packoff.h" DiskId="1"
                Name="cl_packoff.h"
                Source="..\bin\Inc\complib\cl_packoff.h" />
          <File Id="cl_packon.h" DiskId="1"
                Name="cl_packon.h"
                Source="..\bin\Inc\complib\cl_packon.h" />
          <File Id="cl_passivelock.h" DiskId="1"
                Name="cl_passivelock.h"
                Source="..\bin\Inc\complib\cl_passivelock.h" />
          <File Id="cl_perf.h" DiskId="1"
                Name="cl_perf.h"
                Source="..\bin\Inc\complib\cl_perf.h" />
          <File Id="cl_pool.h" DiskId="1"
                Name="cl_pool.h"
                Source="..\bin\Inc\complib\cl_pool.h" />
          <File Id="cl_ptr_vector.h" DiskId="1"
                Name="cl_ptr_vector.h"
                Source="..\bin\Inc\complib\cl_ptr_vector.h" />
          <File Id="cl_qcomppool.h" DiskId="1"
                Name="cl_qcomppool.h"
                Source="..\bin\Inc\complib\cl_qcomppool.h" />
          <File Id="cl_qlist.h" DiskId="1"
                Name="cl_qlist.h"
                Source="..\bin\Inc\complib\cl_qlist.h" />
          <File Id="cl_qlockpool.h" DiskId="1"
                Name="cl_qlockpool.h"
                Source="..\bin\Inc\complib\cl_qlockpool.h" />
          <File Id="cl_qmap.h" DiskId="1"
                Name="cl_qmap.h"
                Source="..\bin\Inc\complib\cl_qmap.h" />
          <File Id="cl_qpool.h" DiskId="1"
                Name="cl_qpool.h"
                Source="..\bin\Inc\complib\cl_qpool.h" />
          <File Id="cl_rbmap.h" DiskId="1"
                Name="cl_rbmap.h"
                Source="..\bin\Inc\complib\cl_rbmap.h" />
          <File Id="cl_reqmgr.h" DiskId="1"
                Name="cl_reqmgr.h"
                Source="..\bin\Inc\complib\cl_reqmgr.h" />
          <File Id="cl_spinlock.h" DiskId="1"
                Name="cl_spinlock.h"
                Source="..\bin\Inc\complib\cl_spinlock.h" />
          <File Id="cl_spinlock_osd.h" DiskId="1"
                Name="cl_spinlock_osd.h"
                Source="..\bin\Inc\complib\cl_spinlock_osd.h" />
          <File Id="cl_thread.h" DiskId="1"
                Name="cl_thread.h"
                Source="..\bin\Inc\complib\cl_thread.h" />
          <File Id="cl_thread_osd.h" DiskId="1"
                Name="cl_thread_osd.h"
                Source="..\bin\Inc\complib\cl_thread_osd.h" />
          <File Id="cl_threadpool.h" DiskId="1"
                Name="cl_threadpool.h"
                Source="..\bin\Inc\complib\cl_threadpool.h" />
          <File Id="cl_timer.h" DiskId="1"
                Name="cl_timer.h"
                Source="..\bin\Inc\complib\cl_timer.h" />
          <File Id="cl_timer_osd.h" DiskId="1"
                Name="cl_timer_osd.h"
                Source="..\bin\Inc\complib\cl_timer_osd.h" />
          <File Id="cl_types.h" DiskId="1"
                Name="cl_types.h"
                Source="..\bin\Inc\complib\cl_types.h" />
          <File Id="cl_types_osd.h" DiskId="1"
                Name="cl_types_osd.h"
                Source="..\bin\Inc\complib\cl_types_osd.h" />
          <File Id="cl_vector.h" DiskId="1"
                Name="cl_vector.h"
                Source="..\bin\Inc\complib\cl_vector.h" />
          <File Id="cl_waitobj.h" DiskId="1"
                Name="cl_waitobj.h"
                Source="..\bin\Inc\complib\cl_waitobj.h" />
          <File Id="cl_waitobj_osd.h" DiskId="1"
                Name="cl_waitobj_osd.h"
                Source="..\bin\Inc\complib\cl_waitobj_osd.h" />
          <File Id="comp_lib.h" DiskId="1"
                Name="comp_lib.h"
                Source="..\bin\Inc\complib\comp_lib.h" />
        </Component>
      </Directory>

      <Directory Id="IB_iba" Name="iba">
        <Component Id="cIBAL_IBA"
                   Guid="2EC5C324-734D-4a06-AE35-D7B80D76B77A">
          <CreateFolder />
          <File Id="ib_al.h" DiskId="1"
                Name="ib_al.h"
                Source="..\bin\Inc\iba\ib_al.h" />
          <File Id="ib_al_ioctl.h" DiskId="1"
                Name="ib_al_ioctl.h"
                Source="..\bin\Inc\iba\ib_al_ioctl.h" />
          <File Id="ib_at_ioctl.h" DiskId="1"
                Name="ib_at_ioctl.h"
                Source="..\bin\Inc\iba\ib_at_ioctl.h" />
          <File Id="ib_ci.h" DiskId="1"
                Name="ib_ci.h"
                Source="..\bin\Inc\iba\ib_ci.h" />
          <File Id="ib_types.h" DiskId="1"
                Name="ib_types.h"
                Source="..\bin\Inc\iba\ib_types.h" />
          <File Id="ib_uvp.h" DiskId="1"
                Name="ib_uvp.h"
                Source="..\bin\Inc\iba\ib_uvp.h" />
        </Component>
      </Directory>

      <Directory Id="IB_user_etc" Name="etc">
        <Component Id="cINC_user_etc"
             Guid="95B9A93E-01A5-44cb-8F90-05812B2DE003">
          <CreateFolder />
          <File Id="getopt.c" DiskId="1"
                Name="getopt.c"
                Source="..\bin\Inc\etc\user\getopt.c" />
          <File Id="gtod.c" DiskId="1"
                Name="gtod.c"
                Source="..\bin\Inc\etc\user\gtod.c" />
          <File Id="inet.c" DiskId="1"
                Name="inet.c"
                Source="..\bin\Inc\etc\user\inet.c" />
          <File Id="search.c" DiskId="1"
                Name="search.c"
                Source="..\bin\Inc\etc\user\search.c" />
        </Component>
      </Directory>

      <Directory Id="IB_user_ib" Name="infiniband">
        <Component Id="cINC_user_ib"
             Guid="0BC9A12D-5808-439f-977F-D28D02317091">
          <CreateFolder />
          <File Id="verbs.h" DiskId="1"
                Name="verbs.h"
                Source="..\bin\Inc\infiniband\verbs.h" />
          <File Id="sa.h" DiskId="1"
                Name="sa.h"
                Source="..\bin\Inc\infiniband\sa.h" />
        </Component>
      </Directory>

      <Directory Id="IB_linux" Name="linux">
        <Component Id="cINC_linux"
                   Guid="BC479552-6293-4baf-87AE-3C0275A3D44C">
          <CreateFolder />
          <File Id="_errno.h" DiskId="1"
                Name="_errno.h"
                Source="..\bin\Inc\linux\_errno.h" />
          <File Id="_string.h" DiskId="1"
                Name="_string.h"
                Source="..\bin\Inc\linux\_string.h" />
          <File Id="inttypes.h" DiskId="1"
                Name="inttypes.h"
                Source="..\bin\Inc\linux\inttypes.h" />
          <File Id="netdb.h" DiskId="1"
                Name="netdb.h"
                Source="..\bin\Inc\linux\netdb.h" />
          <File Id="search.h" DiskId="1"
                Name="search.h"
                Source="..\bin\Inc\linux\search.h" />
          <File Id="unistd.h" DiskId="1"
                Name="unistd.h"
                Source="..\bin\Inc\linux\unistd.h" />
        </Component>

        <Directory Id="IB_linux_arpa" Name="arpa">
          <Component Id="cINC_linux_arpa"
                 Guid="2CAE74DE-D325-487a-9985-39EF982D45AD">
            <CreateFolder />
            <File Id="inet.h" DiskId="1"
                  Name="inet.h"
                  Source="..\bin\Inc\linux\arpa\inet.h" />
          </Component>
        </Directory>

        <Directory Id="IB_linux_netinet" Name="netinet">
          <Component Id="cINC_linux_netinet"
                 Guid="C9C68DEB-C004-4b83-9F3A-F33E1ABB5E86">
            <CreateFolder />
            <File Id="in.h" DiskId="1"
                  Name="in.h"
                  Source="..\bin\Inc\linux\netinet\in.h" />
          </Component>
        </Directory>

        <Directory Id="IB_linux_sys" Name="sys">
          <Component Id="cINC_linux_sys"
                 Guid="F5819C72-412E-4853-88F7-33A1B2635903">
            <CreateFolder />
            <File Id="socket.h" DiskId="1"
                  Name="socket.h"
                  Source="..\bin\Inc\linux\sys\socket.h" />
            <File Id="time.h" DiskId="1"
                  Name="time.h"
                  Source="..\bin\Inc\linux\sys\time.h" />
          </Component>
        </Directory>

      </Directory>

      <Directory Id="IB_user_rdma" Name="rdma">
        <Component Id="cINC_user_rdma"
             Guid="FE5F3635-2D94-4572-91E4-821C9540BA4E">
          <CreateFolder />
          <File Id="rdma_cma.h" DiskId="1"
                Name="rdma_cma.h"
                Source="..\bin\Inc\rdma\rdma_cma.h" />
          <File Id="rdma_verbs.h" DiskId="1"
                Name="rdma_verbs.h"
                Source="..\bin\Inc\rdma\rdma_verbs.h" />
          <File Id="rsocket.h" DiskId="1"
                Name="rsocket.h"
                Source="..\bin\Inc\rdma\rsocket.h" />
          <File Id="rwinsock.h" DiskId="1"
                Name="rwinsock.h"
                Source="..\bin\Inc\rdma\rwinsock.h" />
          <File Id="winverbs.h" DiskId="1"
                Name="winverbs.h"
                Source="..\bin\Inc\rdma\winverbs.h" />
          <File Id="wvstatus.h" DiskId="1"
                Name="wvstatus.h"
                Source="..\bin\Inc\rdma\wvstatus.h" />
        </Component>
      </Directory>

    </Directory>

    <Directory Id="IB_samples" Name="Samples">
      <Directory Id="sample_WDK" Name="WDK">
        <Component Id="cWDK_example"
                   Guid="8C27B926-FCFE-4f46-83B4-A27482A5EB3B">
          <CreateFolder />
              <File Id="cmtest.c" DiskId="1"
                    Name="cmtest.c"
                    Source="..\bin\misc\cmtest.c" />
              <File Id="cmtest.rc" DiskId="1"
                    Name="cmtest.rc"
                    Source="..\..\SDK_Samples\WDK\cmtest.rc" />
              <File Id="makefile" DiskId="1"
                    Name="makefile"
                    Source="..\..\SDK_Samples\WDK\makefile" />
              <File Id="SOURCES" DiskId="1"
                    Name="SOURCES"
                    Source="..\..\SDK_Samples\WDK\SOURCES" />
              <File Id="README.txt" DiskId="1"
                    Name="README.txt"
                    Source="..\..\SDK_Samples\WDK\README.txt" />

              <!-- remove build generated files -->
              <RemoveFile Id='wdk_bld1_file1' On='uninstall'
                          Name='cmtest.exe' />
              <RemoveFile Id='wdk_bld1_file2' On='uninstall'
                          Name='cmtest.res' />
              <RemoveFile Id='wdk_bld1_file3' On='uninstall'
                          Name='cmtest.obj' />
        </Component>
      </Directory>    <!-- End WDK -->

      <Directory Id="sample_VS_bld1" Name="cmtest">
        <Component Id="cVS_bld_example1"
                   Guid="35E03D7E-E943-4346-8F63-7E991AF68F53">
          <CreateFolder />
              <File Id="vs_cmtest.c" DiskId="1"
                    Name="cmtest.c"
                    Source="..\bin\misc\cmtest.c" />
              <File Id="vs_cmtest.rc" DiskId="1"
                    Name="cmtest.rc"
                    Source="..\..\SDK_Samples\cmtest\cmtest.rc" />
              <File Id="vsREADME.txt" DiskId="1"
                    Name="README.txt"
                    Source="..\..\SDK_Samples\cmtest\README.txt" />
              <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
                  <File Id="Makefile.cmtest" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\cmtest\Makefile.x64" />
              <?else?>
                  <File Id="Makefile.cmtest" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\cmtest\Makefile.x86" />
              <?endif?>

              <!-- remove build generated files -->
              <RemoveFile Id='vs_bld1_file1' On='uninstall'
                          Name='cmtest.exe' />
              <RemoveFile Id='vs_bld1_file2' On='uninstall'
                          Name='cmtest.res' />
              <RemoveFile Id='vs_bld1_file3' On='uninstall'
                          Name='cmtest.obj' />
        </Component>
      </Directory>   <!-- End VS_bld_example1 -->

      <Directory Id="sample_VS_bld2" Name="rdma_bw">
        <Component Id="cVS_bld_example2"
                   Guid="CA59CFD2-D51B-4392-8827-E9DFE9AD036F">
          <CreateFolder />
              <File Id="vs_perftest.c" DiskId="1"
                    Name="perftest.c"
                    Source="..\..\..\..\tests\perftest\perftest.c" />
              <File Id="vs_perftest.h" DiskId="1"
                    Name="perftest.h"
                    Source="..\..\..\..\tests\perftest\perftest.h" />
              <File Id="vs_rdma_bw.c" DiskId="1"
                    Name="rdma_bw.c"
                    Source="..\..\..\..\tests\perftest\rdma_bw\rdma_bw.c" />
              <File Id="vs_rdma_bw.rc" DiskId="1"
                    Name="rdma_bw.rc"
                    Source="..\..\SDK_Samples\rdma_bw\rdma_bw.rc" />
              <File Id="vsREADME_rdma_bw.txt" DiskId="1"
                    Name="README.txt"
                    Source="..\..\SDK_Samples\rdma_bw\README.txt" />
              <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
                  <File Id="Makefile.rdma_bw" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\rdma_bw\Makefile.x64" />
              <?else?>
                  <File Id="Makefile.rdma_bw" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\rdma_bw\Makefile.x86" />
              <?endif?>

              <!-- remove build generated files -->
              <RemoveFile Id='vs_bld2_file1' On='uninstall'
                          Name='ibv_rdma_bw.exe' />
              <RemoveFile Id='vs_bld2_file2' On='uninstall'
                          Name='rdma_bw.res' />
              <RemoveFile Id='vs_bld2_file3' On='uninstall'
                          Name='rdma_bw.obj' />
              <RemoveFile Id='vs_bld2_file4' On='uninstall'
                          Name='perftest.obj' />
        </Component>
      </Directory>   <!-- End VS_bld_example2 -->

      <Directory Id="sample_VS_bld3" Name="rstream">
        <Component Id="cVS_bld_example3"
                   Guid="FF858547-E54E-48A9-9BE0-FDC196DFD94B">
          <CreateFolder />
              <File Id="vs_rstream.c" DiskId="1"
                    Name="rstream.c"
                    Source="..\..\SDK_Samples\rstream\rstream.c" />
              <File Id="vs_rstream.rc" DiskId="1"
                    Name="rstream.rc"
                    Source="..\..\SDK_Samples\rstream\rstream.rc" />
              <File Id="vsREADME_rstream.txt" DiskId="1"
                    Name="README.txt"
                    Source="..\..\SDK_Samples\rstream\README.txt" />
              <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
                  <File Id="Makefile.rstream" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\rstream\Makefile.x64" />
              <?else?>
                  <File Id="Makefile.rstream" DiskId="1"
                        Name="Makefile"
                        Source="..\..\SDK_Samples\rstream\Makefile.x86" />
              <?endif?>

              <!-- remove build generated files -->
              <RemoveFile Id='vs_bld3_file1' On='uninstall'
                          Name='rstream.exe' />
              <RemoveFile Id='vs_bld3_file2' On='uninstall'
                          Name='rstream.res' />
              <RemoveFile Id='vs_bld3_file3' On='uninstall'
                          Name='rstream.obj' />
        </Component>
      </Directory>   <!-- End VS_bld_example3 -->

    </Directory>   <!-- End Samples -->

  </Directory>

</Include>
