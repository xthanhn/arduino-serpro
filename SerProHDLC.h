/*
 SerPro - A serial protocol for arduino intercommunication
 Copyright (C) 2009 Alvaro Lopes <alvieboy@alvie.com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 3 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General
 Public License along with this library; if not, write to the
 Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301 USA
 */

/*

 Implementation according to ISO4335 extracted from:
 http://www.acacia-net.com/wwwcla/protocol/iso_4335.htm

 */

#ifndef __SERPRO_HDLC__
#define __SERPRO_HDLC__

#include <inttypes.h>
#include "crc16.h"


#ifndef AVR
#include <stdio.h>
#define LOG(m...) fprintf(stderr,m);
#else
#define LOG(m...)
#endif

// These four templates help us to choose a good storage class for
// the receiving buffer size, based on the maximum message size.

template<unsigned int number>
struct number_of_bytes {
	static unsigned int const bytes = number/256 > 1 ? 2 : 1;
};

template<unsigned int>
struct best_storage_class {
};

template<>
struct best_storage_class<1> {
	typedef uint8_t type;
};

template<>
struct best_storage_class<2> {
	typedef uint16_t type;
};


template<class Config,class Serial,class Implementation> class SerProHDLC
{
public:
	static uint8_t const frameFlag = 0x7E;
	static uint8_t const escapeFlag = 0x7D;
	static uint8_t const escapeXOR = 0x20;

	/* Buffer */
	static unsigned char pBuf[Config::maxPacketSize];

	typedef CRC16_rfc1549 CRCTYPE;
	typedef CRCTYPE::crc_t crc_t;

	static CRCTYPE incrc,outcrc;

	typedef uint8_t command_t;


	typedef typename best_storage_class< number_of_bytes<Config::maxPacketSize>::bytes >::type buffer_size_t;
	//typedef uint16_t buffer_size_t;
	typedef uint16_t packet_size_t;

	static buffer_size_t pBufPtr;
	static packet_size_t pSize,lastPacketSize;

	/* HDLC parameters extracted from frame */
	static uint8_t inAddressField;
	static uint8_t inControlField;

	/* HDLC control data */
	static uint8_t txSeqNum;        // Transmit sequence number
	static uint8_t rxNextSeqNum;    // Expected receive sequence number

	static bool unEscaping;
	static bool forceEscaping;
    static bool inPacket;

	struct RawBuffer {
		unsigned char *buffer;
		buffer_size_t size;
	};

	struct HDLC_header {
		uint8_t address;
		union {
			struct {
				uint8_t flag: 1;   /* Always zero */
				uint8_t txseq: 3;  /* TX sequence number */
				uint8_t poll: 1;   /* poll/final */
				uint8_t rxseq: 3;  /* RX sequence number */
			} iframe;

			struct {
				uint8_t flag: 2;   /* '1''0' for S-frame */
				uint8_t function: 2; /* Supervisory function */
				uint8_t poll: 1;     /* Poll/Final */
				uint8_t seq: 3;    /* Sequence number */
			} sframe;

			struct {
				uint8_t flag: 2;   /* '1''1' for U-Frame */
				uint8_t modifier: 2; /* Modifier bits */
				uint8_t poll: 1;   /* Poll/Final */
				uint8_t function: 3; /* Function */
			} uframe;

			struct {
				uint8_t flag: 2;  /* LSB-MSB:  0X: I-frame, 10: S-Frame, 11: U-Frame */
				uint8_t unused: 6;
			} frame_type;

		} control;
	};


	static inline void setForceEscape(bool a)
	{
		forceEscaping=a;
	}

	static inline void dumpPacket() { /* Debuggin only */
		unsigned i;
		LOG("Packet: %d bytes\n", lastPacketSize);
		LOG("Dump (hex): ");
		for (i=0; i<lastPacketSize; i++) {
			LOG("0x%02X ", (unsigned)pBuf[i+1]);
		}
		LOG("\n");
	}
	static inline RawBuffer getRawBuffer()
	{
		RawBuffer r;
		r.buffer = pBuf+3;
		r.size = lastPacketSize;
		LOG("getRawBuffer() : size %u %u\n", r.size,lastPacketSize);
		return r;
	}

	static inline void sendByte(uint8_t byte)
	{
		if (byte==frameFlag || byte==escapeFlag || forceEscaping) {
			Serial::write(escapeFlag);
			Serial::write(byte ^ escapeXOR);
		} else
			Serial::write(byte);
	}

	/* Frame types */
	enum frame_type {
		FRAME_INFORMATION,
		FRAME_SUPERVISORY,
		FRAME_UNNUMBERED
	};


	/* Supervisory commands */
	enum supervisory_command {
		RR  =  0x0,   // Receiver ready
		RNR =  0x1,   // Receiver not ready
		REJ =  0x2,   // Reject
		SREJ = 0x3    // Selective-reject
	};


	/* This field is actually a combination of
	 modifier plus function type. We number then
	 according to concatenation of both */
	enum unnumbered_command {
		UI          = 0x00, // 00-000 Unnumbered Information
		SNRM        = 0x01, // 00-001 Set Normal Response Mode
		RD          = 0x02, // 00-010 Request Disconnect
        // Not used 00-011
		UP          = 0x04, // 00-100 Unnumbered Poll
		// Not used 00-101
		UA          = 0x06, // 00-110 Unnumbered Acknowledgment
		TEST        = 0x07, // 00-111 Test
        // All 01-XXX are not used
		RIM         = 0x10, // 10-000 Request Initialization Mode
		FRMR        = 0x11, // 10-001 Frame Reject
		SIM         = 0x12,  // 10-010 Set Initialization Mode
		// Not used any other 10-XXX

		SARM, // Set Asynchronous Response Mode
		DM, // Disconnected Mode
		SABM, // Set Asynchronous Balanced Mode
		
	//	RD, //  Request Disconnect
		SNRME, // Set Normal Response Mode Extended

		SARME, // Set Asynchronous Response Mode Extended
		SABME, // Set Asynchronous Balanced Mode Extended

		
		XID, // Exchange identification
		RSET // Reset

	};

	static inline void sendInformationControlField()
	{
		//sendByte( txSeqNum<<1 | rxNextSeqNum<<5 );
		//outcrc.update( txSeqNum<<1 | rxNextSeqNum<<5 );
		sendByte(0x3);
		outcrc.update(0x3);
	}

	static void startPacket(packet_size_t len)
	{
		outcrc.reset();
		pBufPtr=0;
	}

	static void sendPreamble()
	{
		Serial::write( frameFlag );
		sendByte( (uint8_t)Config::stationId );
		outcrc.update( (uint8_t)Config::stationId );
		sendInformationControlField();
	}

	static void sendPostamble()
	{
		CRC16_ccitt::crc_t crc = outcrc.get();
		sendByte(crc & 0xff);
		sendByte(crc>>8);
		Serial::write(frameFlag);
		Serial::flush();
		txSeqNum++;
		txSeqNum&=0x7; // Cap at 3-bits only.
	}

	static void sendData(const unsigned char * const buf, packet_size_t size)
	{
		packet_size_t i;
		LOG("Sending %d payload\n",size);
		for (i=0;i<size;i++) {
			outcrc.update(buf[i]);
			sendByte(buf[i]);
		}
	}

	static void sendData(unsigned char c)
	{
		outcrc.update(c);
		sendByte(c);
	}

	static void sendPacket(command_t const command, unsigned char * const buf, packet_size_t const size)
	{
		startPacket(size);
		sendPreamble();
		outcrc.update( command );
		sendData(command);
		sendData(buf,size);
		sendPostamble();
	}

	static void sendCommandPacket(command_t const command, unsigned char * const buf, packet_size_t const size)
	{
		startPacket(size);
		sendPreamble();
		outcrc.update( command );
		sendData(command);
		sendData(buf,size);
		sendPostamble();
	}

	static void preProcessPacket()
	{
		/* Check CRC */
		if (pBufPtr<4) {
			/* Empty/erroneous packet */
			LOG("Short packet received, len %u\n",pBufPtr);
			return;
		}

		/* Make sure packet is meant for us. We can safely check
		 this before actually computing CRC */

		packet_size_t i;
		incrc.reset();
		for (i=0;i<pBufPtr-2;i++) {
			incrc.update(pBuf[i]);
		}
		crc_t pcrc = *((crc_t*)&pBuf[pBufPtr-2]);
		if (pcrc!=incrc.get()) {
			/* CRC error */
			LOG("CRC ERROR, expected 0x%04x, got 0x%04x\n",incrc.get(),pcrc);
			return;
		}
		LOG("CRC MATCH 0x%04x, got 0x%04x\n",incrc.get(),pcrc);
		lastPacketSize = pBufPtr-4;
		LOG("Packet details: destination ID %u, control %02x\n", pBuf[0],pBuf[1]);
		Implementation::processPacket(pBuf+2,pBufPtr-4);

		pBufPtr=0;
	}

	static void processData(uint8_t bIn)
	{
		LOG("Process data: %d\n",bIn);
		if (bIn==escapeFlag) {
			unEscaping=true;
			return;
		}

		// Check unescape error ?
		if (bIn==frameFlag && !unEscaping) {
			if (inPacket) {
				/* End of packet */
				if (pBufPtr) {
					preProcessPacket();
					inPacket = false;
				}
			} else {
				/* Beginning of packet */
				pBufPtr = 0;
				inPacket = true;
				incrc.reset();
			}
		} else {
			if (unEscaping) {
				bIn^=escapeXOR;
				unEscaping=false;
			}

			if (pBufPtr<Config::maxPacketSize) {
				pBuf[pBufPtr++]=bIn;
			} else {
				// Process overrun error
			}
		}
	}
};

#define IMPLEMENT_PROTOCOL_SerProHDLC(SerPro) \
	template<> SerPro::MyProtocol::buffer_size_t SerPro::MyProtocol::pBufPtr=0; \
	template<> uint8_t SerPro::MyProtocol::txSeqNum=0; \
	template<> uint8_t SerPro::MyProtocol::rxNextSeqNum=0; \
	template<> SerPro::MyProtocol::packet_size_t SerPro::MyProtocol::pSize=0; \
	template<> SerPro::MyProtocol::packet_size_t SerPro::MyProtocol::lastPacketSize=0; \
	template<> SerPro::MyProtocol::CRCTYPE SerPro::MyProtocol::incrc=CRCTYPE(); \
	template<> SerPro::MyProtocol::CRCTYPE SerPro::MyProtocol::outcrc=CRCTYPE(); \
	template<> bool SerPro::MyProtocol::unEscaping = false; \
	template<> bool SerPro::MyProtocol::forceEscaping = false; \
	template<> bool SerPro::MyProtocol::inPacket = false; \
	template<> unsigned char SerPro::MyProtocol::pBuf[]={0};

#endif
