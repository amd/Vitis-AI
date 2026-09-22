# MLDebugger uses git-lfs for large binaries. Hosts may not have git-lfs
# installed or allowed by policy. lfs=0 skips LFS downloads; FETCHCMD_git
# disables the LFS clean/smudge/process filters so checkout still succeeds
# (pointer stubs land in the tree; callers overlay real blobs separately).
MLDEBUGGER_GIT_URI = "git://github.com/Xilinx/MLDebugger.git;branch=main;protocol=https;lfs=0"
FETCHCMD_git = "git -c gc.autoDetach=false -c core.pager=cat -c safe.bareRepository=all -c clone.defaultRemoteName=origin -c filter.lfs.process= -c filter.lfs.smudge= -c filter.lfs.clean= -c filter.lfs.required=false"
