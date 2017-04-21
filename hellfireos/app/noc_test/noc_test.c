#include <hellfire.h>
#include <noc.h>

void sender(void)
{
	int32_t msg = 0;
	int8_t buf[1500];
	uint16_t val;
	
	if (hf_comm_create(hf_selfid(), 1000, 0))
		panic(0xff);
	
	while (1){
		sprintf(buf, "i am cpu %d, thread %d: msg %d size: %d\n", hf_cpuid(), hf_selfid(), msg++, sizeof(buf));
		val = hf_sendack(3, 5000, buf, sizeof(buf), 0, 500);
		if (val)
			printf("hf_sendack(): error %d\n", val);
	}
}

void receiver(void)
{
	int8_t buf[1500];
	uint16_t cpu, task, size, val;
	
	if (hf_comm_create(hf_selfid(), 5000, 0))
		panic(0xff);
	
	while (1){
		val = hf_recvack(&cpu, &task, buf, &size, 0);
		if (val)
			printf("hf_recvack(): error %d\n", val);
		else
			printf("%s", buf);
	}
}

void app_main(void)
{
	if (hf_cpuid() == 2){
		hf_spawn(sender, 0, 0, 0, "sender", 4096);
	}else{
		hf_spawn(receiver, 0, 0, 0, "receiver", 4096);
	}
}
