#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "drivers/platform.h"
#include "drivers/uart.h"
#include "drivers/queue.h"
#include "drivers/timer.h"
#include "drivers/gpio.h"

#define BUFF_SIZE 128

//uart_rx_isr stores the characters that come from UART peripheral in this queue. 
//Later in the main function, these characters are fetched and stored in the main buffer
Queue rx_queue;		

//active profile string, copied from buff before execution starts,
//so the user can type a new profile while the current one runs
volatile char timer_buff[BUFF_SIZE];		

//used for toggling the led between 1 and 0
volatile bool led_state = 0;		

//set to 1 while the user is actively entering a profile 
volatile bool is_typing = 0;	

//set to 1 while a profile is executing inside timer_isr
volatile bool profile_running = 0;
	
//set to 1 by timer_isr when the last digit of a profile finishes
volatile bool profile_finished = 0;
	
// state machine for the emergency-stop button:
//   0 = normal
//   1 = E-stop active 
//   2 = override requested 
volatile uint32_t emergency_button_pressed_counter = 0;	

//counts 10 ms ticks during state 2; system returns to state 1 after 500 ticks (5s)
volatile uint32_t unlock_timeout_counter = 0;		

//index that points to the current character of the profile, that is executing
volatile uint32_t current_digit_index = 0;		

//counts 10 ms ticks since the user started typing; cleared on each keystroke.
//Profile is discarded if it reaches 400 ticks (4s) with no input
volatile uint32_t four_second_timeout_counter = 0;		

//on each timer interrupt, this counter is increased, and is used for comparison 
//with the target_ticks that there are according to the current digit, so the program knows when to toggle the led
volatile uint32_t led_toggle_phase_counter = 0;		

//counts ticks spent on the current digit; digit advances at 200 ticks (2s)
volatile uint32_t digit_active_window_counter = 0;		


//Whenever a character is received by the UART peripheral,
//an interrupt is occured that fetches that character and 
//stores it on a queue data stracture. if in normal state only
//the integers 0-9,-, backspace and enter characters are accepted.
//If in E-stop state, every character is ignored. If in override requested
//state, every character is accepted.
void uart_rx_isr(uint8_t rx) {
	if (emergency_button_pressed_counter == 0) {
		if ((rx>=0x30 && rx<=0x39) || rx == 0x0D || rx == 0x08 || rx == 0x2D) {
			queue_enqueue(&rx_queue, rx);
		}
	}else if (emergency_button_pressed_counter == 2) {
		queue_enqueue(&rx_queue, rx);
	}
}

//The timer_isr should be executed periodically,
//according to the period specified by the timer_init instruction.
//For the needs of the exercise (we have to use
//only one timer) we use one timer with a stable timestamp, which
//does multiple tasks.
void timer_isr() {
	if (is_typing) {
		four_second_timeout_counter++;
	}
	
	if (profile_running) {
		
		//If the digit is hyphen-minus, this means that the previous sequence of digits needs to be executed again.
		if (timer_buff[current_digit_index] == 0x2D) {
			current_digit_index = 0;
		}
			
		//If the digit is the null-terminator character, the profile stops executing (profile_running=0, profile_finished=1)
		if (timer_buff[current_digit_index] == '\0') {
			profile_finished = 1;
			profile_running = 0;
			gpio_set(PA_5, 0);
		}
		
		digit_active_window_counter++;	
		int freq = timer_buff[current_digit_index] - '0';
		
		//checks the frequency that the led needs to be toggled 
		if (freq > 0) {
			led_toggle_phase_counter++;
			
			//timer interrupts are occurred every 10ms, so we have 100 ticks in every second. In order LED toggles with f frequency, 
			//the time period is T = 1/f. So half-period is 1/(2f). This means: Ticks = 100/(2f) => Ticks = 50/f
			uint32_t target_ticks = (50 / freq);		
			
			//comparison of the led_toggle_phase_counter with the target_ticks, so the led 
			//is toggled on a standard frequency depending on the current character of the profile
			if (led_toggle_phase_counter >= target_ticks) {
				gpio_toggle(PA_5);
				led_toggle_phase_counter = 0;
			}
		}else {
			gpio_set(PA_5, 0);
		 }
		
		//each digit runs for 2s (200 ticks at 10ms/tick), then current_digit_index is increasded, pointing to the next character
		if (digit_active_window_counter >= 200) {
			digit_active_window_counter = 0;
			led_toggle_phase_counter = 0;
			current_digit_index++;
		}
	}
	
	if (emergency_button_pressed_counter == 2) {
		unlock_timeout_counter++;
	}
}

//Whenever a the B1 button (P_SW) is pressed, the p_sw_pressed_isr routine is called.
void p_sw_pressed_isr() {
	if (emergency_button_pressed_counter == 0) {
		timer_disable();
		
		//for flushing the queue. Avoids queue_init() to prevent heap allocations (malloc), which caused crashes after multiple E-stops
		rx_queue.head = 0;		
		rx_queue.tail = 0;		
		
		gpio_set(PA_5, 1);
		is_typing = 0;
		profile_running = 0;
		profile_finished = 0;
		current_digit_index = 0;	
		four_second_timeout_counter = 0;	
		led_toggle_phase_counter = 0;
		digit_active_window_counter = 0;
		emergency_button_pressed_counter++;
	} else if (emergency_button_pressed_counter == 1) {
		timer_enable();
		emergency_button_pressed_counter++;
	}
}


int main() {
	
	//the character that is fetched from rx_queue is stored in the rx_char
	uint8_t rx_char = 0;		
	
	//the buffer that the rx_char is eventually stored
	char buff[BUFF_SIZE];		
	
	uint32_t buff_index = 0;	
	
	gpio_set_mode(P_SW, Input);
	gpio_set_trigger(P_SW, Falling);
	gpio_set_callback(P_SW, p_sw_pressed_isr);		
	
	gpio_set_mode(PA_5, Output);
	gpio_set(PA_5, 0);
	
	timer_init(10000);
	timer_enable();
	timer_set_callback(timer_isr);		
	
	queue_init(&rx_queue, 128);
	
	uart_init(115200);
	uart_set_rx_callback(uart_rx_isr);		
	uart_enable();
	
	//priority order: 0 is the highest, 10 is the lowest and 5 is the middle one
	NVIC_SetPriority(EXTI15_10_IRQn, 0);
	NVIC_SetPriority(SysTick_IRQn, 5);
	NVIC_SetPriority(USART2_IRQn, 10);
	__enable_irq();
		
	while(1) {
		
		if (emergency_button_pressed_counter == 0) {
			buff_index = 0;
			is_typing = 0;
			uart_print("\r\nEnter your profile (integers: 0-9)->");
		}
		
		do {
			
			//while there is no character stored in the queue.
			while (!queue_dequeue(&rx_queue, &rx_char)) {	
				
				//CPU enters "sleep mode" until an interrupt is occurred.
				__WFI();		
				
				if (emergency_button_pressed_counter == 1) {
					uart_print("\r\n[ERROR] SYSTEM LOCKED\r\n");
				}
				
				//print override prompt once upon entering state 2
				if (emergency_button_pressed_counter == 2 && unlock_timeout_counter == 1) {	
					uart_print("\r\nOverride requested. Awaiting password...\r\n");																							
				}
				
				//switch back to E-stop state, if the user didn't enter the right password in the 5 second window
				if (unlock_timeout_counter >= 500) {	
					unlock_timeout_counter = 0;
					emergency_button_pressed_counter = 1;	
					buff_index = 0;
					timer_disable();
					uart_print("\r\n[TIMEOUT]\r\n\r\n[ERROR] SYSTEM LOCKED\r\n");		
				}
				
				if (profile_finished) {	
					profile_finished = 0;
					uart_print("\r\nYour profile has finished\r\n\r\nEnter your profile (integers: 0-9)->");
				}
				
				//discard the partially typed profile if the user is idle for 4s
				if (is_typing && four_second_timeout_counter >= 400) {
					buff_index = 0;
					four_second_timeout_counter = 0;
					is_typing = 0;
					uart_print("\r\n[TIMEOUT] Sequence cleared! Start again. \r\n");
					uart_print("\r\nEnter your profile (integers: 0-9)->");
				}
			}
			
			//this if condition is rarely executed, but we add it, just in case, the p_sw interrupt is occured just before that if condition
			if (emergency_button_pressed_counter == 1) {	
				rx_char = 0;
				continue;
			}
			
			//reset inactivity timeout upon valid profile character input
			if (((rx_char >=0x30 && rx_char <=0x39) || rx_char == 0x2D) && emergency_button_pressed_counter != 2) {	
				four_second_timeout_counter = 0;
				if (!is_typing) {
					is_typing = 1;
				}
			}
			
			if (rx_char == 0x08) {
				if (buff_index > 0) {
					four_second_timeout_counter = 0;
					buff_index--;
					uart_tx(rx_char);
					uart_tx(' ');
					uart_tx(rx_char);
					
					//if the buffer is now empty, the user is no longer actively typing
					if (buff_index == 0 && emergency_button_pressed_counter != 2) {	
						is_typing = 0;
					}
				}
			//hyphen-minus cannot be the first character of a profile
			}else if (buff_index == 0 && rx_char == 0x2D && emergency_button_pressed_counter !=2) {	
				four_second_timeout_counter = 0;
				is_typing = 0;
				uart_print("\r\nThe hyphen-minus cannot be the first character you enter!\r\n\r\nEnter your profile (integers: 0-9)->");
			//hyphen-minus must be the last character of the profile
			}else if (buff[buff_index-1] == 0x2D && ((rx_char >= 0x30 && rx_char <=0x39) || rx_char == 0x2D) && emergency_button_pressed_counter !=2) {	
				four_second_timeout_counter = 0;
				is_typing = 0;
				buff_index = 0;
				uart_print("\r\nThe hyphen-minus must be the last character of the profile!\r\n\r\nEnter your profile (integers: 0-9)->");
			}else {	
				buff[buff_index++] = (char)rx_char;		
				uart_tx(rx_char);		
			}
		}	while(rx_char != '\r' && buff_index < BUFF_SIZE);		
		
			if (rx_char == 0x0D) {
				//special text is printed, if used has not entered any characters, and is in normal state
				if (buff_index == 1 && emergency_button_pressed_counter == 0) {	
					buff[buff_index-1] = '\0';
					is_typing = 0;
					four_second_timeout_counter = 0;
					uart_print("\r\nYou have not entered any characters!\r\n");
				//process valid profile submission
				}else if(buff_index != 1 && emergency_button_pressed_counter == 0) {	
					buff[buff_index-1] = '\0';
					is_typing = 0;
					four_second_timeout_counter = 0;
					
					//stop current profile before loading new one
					profile_running = 0;
					
					gpio_set(PA_5, 0);
					strncpy((char*)timer_buff, buff, BUFF_SIZE);	
					current_digit_index = 0;
					digit_active_window_counter = 0;
					led_toggle_phase_counter = 0;
					
					//writing timer_buff is complete, low timer_isr to start executing it
					profile_running = 1;		
					
					uart_print("\r\nNew profile started!\r\n");
					
				//if program is in override requested state...
				}else if (emergency_button_pressed_counter == 2) {		
					buff[buff_index-1] = '\0';
					if (strcmp(buff, "unlock") == 0) {	
						emergency_button_pressed_counter = 0;
						unlock_timeout_counter = 0;
						gpio_set(PA_5, 0);
						uart_print("\r\nYou have gained access!\r\n");
					}else {
						buff_index = 0;
						uart_print("\r\nYou have entered the wrong password. Try again\r\n");
					}
				}
			}
			
		//print special message in the case of main buffer overflow
		if (buff_index >= BUFF_SIZE) {	
			uart_print("\r\nStop trying to overflow my buffer! I resent that!\r\n");
		}
	}
}
