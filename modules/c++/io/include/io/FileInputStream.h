/* =========================================================================
 * This file is part of io-c++ 
 * =========================================================================
 * 
 * (C) Copyright 2004 - 2009, General Dynamics - Advanced Information Systems
 *
 * io-c++ is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public 
 * License along with this program; If not, 
 * see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __IO_FILE_INPUT_STREAM_H__
#define __IO_FILE_INPUT_STREAM_H__

#if defined(USE_IO_STREAMS)
#    include "io/FileInputStreamIOS.h"
namespace io {
  typedef FileInputStreamIOS FileInputStream;
}
#else
#    include "io/FileInputStreamOS.h"
namespace io {
  typedef FileInputStreamOS FileInputStream;
}
#endif


#endif
