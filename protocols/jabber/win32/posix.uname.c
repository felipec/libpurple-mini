/*
   posix.uname.c - version 1.1
   Copyright (C) 1999, 2000
	     Earnie Boyd and assigns

   Fills the utsname structure with the appropriate values.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation; either version 2.1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICUALR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301, USA.
 */

/*
   Send bug reports to Earnie Boyd <earnie_boyd@yahoo.com>
 */

#include "utsname.h"
#include <string.h>
#include <stdio.h>

/* ANONYMOUS unions and structs are used from the windows header definitions.
   These need to be defined for them to work correctly with gcc2.95.2-mingw. */
/*#define _ANONYMOUS_STRUCT*/
/*#define _ANONYMOUS_UNION*/
#include <windows.h>
#ifdef __MINGW32__
#include <_mingw.h>
#endif

int
jabber_win32_uname( struct utsname *uts )
{
  DWORD sLength;
  OSVERSIONINFO OS_version;
  SYSTEM_INFO System_Info;

/* XXX Should these be in the global runtime */
  enum WinOS {Win95, Win98, WinNT, unknown};
  int MingwOS;

  memset( uts, 0, sizeof ( *uts ) );
  OS_version.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );

  GetVersionEx ( &OS_version );
  GetSystemInfo ( &System_Info );

  strcpy( uts->sysname, "WIN32_" );
  switch( OS_version.dwPlatformId )
  {
    case VER_PLATFORM_WIN32_NT:
      strcat( uts->sysname, "WinNT" );
      MingwOS = WinNT;
      break;
    case VER_PLATFORM_WIN32_WINDOWS:
      switch ( OS_version.dwMinorVersion )
      {
        case 0:
          strcat( uts->sysname, "Win95" );
	  MingwOS = Win95;
          break;
        case 10:
          strcat( uts->sysname, "Win98" );
	  MingwOS = Win98;
          break;
        default:
          strcat( uts->sysname, "Win??" );
	  MingwOS = unknown;
          break;
      }
      break;
    default:
      strcat( uts->sysname, "Win??" );
      MingwOS = unknown;
      break;
  }

#ifdef __MINGW32__
  sprintf( uts->version, "%i", __MINGW32_MAJOR_VERSION );
  sprintf( uts->release, "%i", __MINGW32_MINOR_VERSION );
#endif

  switch( System_Info.wProcessorArchitecture )
  {
    case PROCESSOR_ARCHITECTURE_PPC:
      strcpy( uts->machine, "ppc" );
      break;
    case PROCESSOR_ARCHITECTURE_ALPHA:
      strcpy( uts->machine, "alpha" );
      break;
    case PROCESSOR_ARCHITECTURE_MIPS:
      strcpy( uts->machine, "mips" );
      break;
    case PROCESSOR_ARCHITECTURE_INTEL:
      /* dwProcessorType is only valid in Win95 and Win98
         wProcessorLevel is only valid in WinNT */
      switch( MingwOS )
      {
        case Win95:
	case Win98:
          switch( System_Info.dwProcessorType )
          {
            case PROCESSOR_INTEL_386:
            case PROCESSOR_INTEL_486:
            case PROCESSOR_INTEL_PENTIUM:
              sprintf( uts->machine, "i%ld", System_Info.dwProcessorType );
              break;
            default:
              strcpy( uts->machine, "i386" );
              break;
          }
          break;
        case WinNT:
	  sprintf( uts->machine, "i%d86", System_Info.wProcessorLevel );
	  break;
	default:
	  strcpy( uts->machine, "unknown" );
	  break;
      }
      break;
    default:
      strcpy( uts->machine, "unknown" );
      break;
  }

  sLength = sizeof ( uts->nodename ) - 1;
  GetComputerNameA( uts->nodename, &sLength );
  return 1;
}

