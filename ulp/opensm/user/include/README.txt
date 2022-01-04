

****** WARNING ******

Do not confuse the folders iba\ and complib\ with the folders trunk\inc\complib trunk\inc\iba !

The folders in trunk\inc\ are prefered and searched first when building OpenSM!

The iba\ folder is present to track OFED ib_types.h changes in order to stay in sync; not used in opensm build.

  'In spirit' the files user\include\iba\ib_types.h == inc\iba\ib_types.h are equivalent.

One day into the future, ib_types.h will be split apart where some of those parts end up
in opensm\user\inc\ relevant folders. Point being in ib_types.h contains far too many
disjoint definitions, collect the OpenSM and MAD handling items in respective .h files.