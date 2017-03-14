#!/usr/bin/python
#coding:utf-8
#
#
#

import argparse
import traceback
import sys
import os
import time
import threading
import socket
import struct
import binascii
import ctypes

SEND_FILE_SIZE = 4096
RECV_SIZE = 1024
threads = []

def connectserver(args):
	try:
		conn = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
		conn.connect((args.host,args.port))
		while True:
			if sendfilemete(args.upfile):
				sendfile(conn,args.upfile)
			else:
				print 'send file %s metedata failed.' %(args.upfile)
			recvmsg(conn)
	except socket.error,e:
		print 'str(Exception):\t',str(e)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
	except Exception,e:
		print 'str(Exception):\t',str(Exception)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
	finally:
		conn.close()

def recvmsg(conn):
	try:
		while True:
			recv_buff = conn.recv(RECV_SIZE)
			recv_buff_len = len(recv_buff)
			if recv_buff_len == 0:
				print 'recv no data.\n'
				break
			print 'recv message %s' %(binascii.hexlify(recv_buff))
	except socket.error,e:
		print 'recv server message failed.\n'
		print 'str(Exception):\t',str(e)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
	except Exception,e:
		print 'recv server message failed.\n'
		print 'str(Exception):\t',str(Exception)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()


def sendfile(conn,filename):
	print 'send file %s started...'%(filename)
	try:
		f = open(filename,'rb')
		while True:
			data = f.read(SEND_FILE_SIZE)
			#print 'data %s' %(str(data))
			if not data:
				print 'no data send'
				break;
			conn.sendall(data)
		print 'file %s send success.\n' %(filename)
	except socket.error,e:
		print 'send file %s failed.\n' %(filename)
		print 'str(Exception):\t',str(e)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
	except Exception,e:
		print 'send file %s failed.\n' %(filename)
		print 'str(Exception):\t',str(Exception)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
	finally:
		f.close();


def sendfilemete(filename):
	filesize = 0
	try:
		filesize = os.path.getsize(filename)
		packed_header_data = (144,0x12,0x00)
		packed_header_st = struct.Struct('QII')
		packed_body_data = (filename,filesize,filesize)
		packed_body_st = struct.Struct('>128sQQ')
		packed_header_data_len = struct.calcsize('QII')
		packed_body_data_len = struct.calcsize('>128sQQ')
		packed_buff = ctypes.create_string_buffer(packed_header_data_len + packed_body_data_len);

		print 'packed header size:',packed_header_data_len
		print 'packed header size:',packed_body_data_len

		packed_header_st.pack_into(packed_buff,0,*packed_header_data)
		packed_body_st.pack_into(packed_buff,packed_header_data_len,*packed_body_data)

		print 'packed buff data:',binascii.hexlify(packed_buff)

	except Exception,e:
		print 'send file mete failed.\n'
		print 'str(Exception):\t',str(Exception)
		print 'traceback.format_exc():\n%s' % traceback.format_exc()
		return False

	return True










def main():
	print '************************************************'
	print '*********lfs upload the pressure test***********'
	print '************************************************'

	parser = argparse.ArgumentParser(description="lfs upload pressure test")
	parser.add_argument("--host",required=True,help="master dataserver host ip");
	parser.add_argument("--port",type=int,default=2987,help="master dataserver port");
	parser.add_argument("--threads",type=int,default=1,help="threads count");
	parser.add_argument("--upfile",required=True,help="upload file name");

	args = parser.parse_args()

	for i in range(0,args.threads):
		t = threading.Thread(target=connectserver(args))
		threads.append(t)
	
	for i in range(0,args.threads):
		threads[i].start()
	
	for i in range(0,args.threads):
		threads[i].join()


if __name__ == '__main__':
    main()

