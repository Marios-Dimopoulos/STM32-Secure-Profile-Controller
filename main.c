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

Queue rx_queue;	//uart_rx_isr stores the characters that come from UART peripheral in this queue. Later in the main function, these characters are fetched for acts upon them.

volatile char timer_buff[BUFF_SIZE];	//characters in the main buff are copied in the timer_buff, so the user can enter another profile, while the previous one is executed, and stop it early.
volatile bool led_state = 0;	//used for toggling the led between 1 and 0
volatile bool is_typing = 0;	
volatile bool profile_running = 0;
volatile bool profile_finished = 0;
volatile uint32_t emergency_button_pressed_counter = 0;	
volatile uint32_t unlock_timeout_counter = 0;	//counts the 5 seconds that the user has in order to enter the password "unlock"
volatile uint32_t current_digit_index = 0;	//index that points to the current character of the profile, that is about to execute
volatile uint32_t four_second_timeout_counter = 0;	//counts the 4 seconds that the user has in order to submit the new profile he is creating
volatile uint32_t led_toggle_phase_counter = 0;	//on each timer interrupt, this counter is increased, and is used for comparison with the target_ticks that there are according to the current index, so the program know when to toggle the led
volatile uint32_t digit_active_window_counter = 0;	//counts the 2 seconds that each character/digit has to execute


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
//For the needs of the exercise (we needed to use
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
		
		//This if-else condition checks what is the frequency that the led needs to be toggled 
		if (freq > 0) {
			led_toggle_phase_counter++;
			uint32_t target_ticks = (50 / freq);	//timer interrupts are occured every 10ms, so we have 100 ticks in every second. In order LED toggles with f frequency, the time period is T = 1/f. So half-period is 1/(2f). This means: Ticks = 100/(2f) => Ticks = 50/f.
			
			if (led_toggle_phase_counter >= target_ticks) {
				gpio_toggle(PA_5);
				led_toggle_phase_counter = 0;
			}
		}else {
			gpio_set(PA_5, 0);
		 }
		
		//When the window for a particular digit (frequency) is over, we do curret_digit_index++ (points to the next digit).
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

//Whenever a the B1 button (P_SW) is pressed, the p_sw_pressed_isr
//is executed.
void p_sw_pressed_isr() {
	if (emergency_button_pressed_counter == 0) {
		timer_disable();
		rx_queue.head = 0;	//for flushing the queue. Avoids queue_init() to prevent heap allocations (malloc), which caused crashes after multiple E-stops
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
	uint8_t rx_char = 0;	//the character that is fetched from rx_queue is store in the rx_char
	char buff[BUFF_SIZE];	//the buffer that the rx_char is eventually stored
	uint32_t buff_index = 0;	
	
	gpio_set_mode(P_SW, Input);
	gpio_set_trigger(P_SW, Falling);
	gpio_set_callback(P_SW, p_sw_pressed_isr);	//whenever the B1 button is pressed, the p_sw_pressed_isr preempts the main thread
	
	gpio_set_mode(PA_5, Output);
	gpio_set(PA_5, 0);
	
	timer_init(10000);
	timer_enable();
	timer_set_callback(timer_isr);	//whenever the timer ticks, the timer_isr is executed (in my occasion every 10ms)
	
	queue_init(&rx_queue, 128);
	
	uart_init(115200);
	uart_set_rx_callback(uart_rx_isr);	//whenever a character is received by the UART peripheral, the uart_rx_isr is executed.
	uart_enable();
	
	//setting the isr priorities
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
			while (!queue_dequeue(&rx_queue, &rx_char)) {	//while there is no character stored in the queue.
				__WFI();	//CPU enters "sleep mode" until an interrupt is occured.
				
				if (emergency_button_pressed_counter == 1) {
					uart_print("\r\n[ERROR] SYSTEM LOCKED\r\n");
				}
				
				if (emergency_button_pressed_counter == 2 && unlock_timeout_counter == 1) {	//so the text in the uart_print, is printed only one time, when the first timer interrupt occurs.
					uart_print("\r\nOverride requested. Awaiting password...\r\n");																							
				}
				
				if (unlock_timeout_counter >= 500) {
					unlock_timeout_counter = 0;
					emergency_button_pressed_counter = 1;	//switch back to E-stop state, if the user didnt enter the right password in the 5 second window
					buff_index = 0;
					timer_disable();
					uart_print("\r\n[TIMEOUT]\r\n\r\n[ERROR] SYSTEM LOCKED\r\n");		
				}
				
				if (profile_finished) {	
					profile_finished = 0;
					uart_print("\r\nYour profile has finished\r\n\r\nEnter your profile (integers: 0-9)->");
				}
				
				if (is_typing && four_second_timeout_counter >= 400) {
					buff_index = 0;
					four_second_timeout_counter = 0;
					is_typing = 0;
					uart_print("\r\n[TIMEOUT] Sequence cleared! Start again. \r\n");
					uart_print("\r\nEnter your profile (integers: 0-9)->");
				}
			}
			if (emergency_button_pressed_counter == 1) {	//this if condition is rarely executed, but we add it, just in case, the p_sw interrupt is occured just before that if condition. 
				rx_char = 0;
				continue;
			}
			if (((rx_char >=0x30 && rx_char <=0x39) || rx_char == 0x2D) && emergency_button_pressed_counter != 2) {	//Enter integers (0-9) and hyphen-minus (-), while not being in override requested state
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
					if (buff_index == 0 && emergency_button_pressed_counter != 2) {	//if, because of the backspace, the buffer has no characters in it (the buff_index equals to 0), it is supposed that the user is not typing anything
						is_typing = 0;
					}
				}
			}else if (buff_index == 0 && rx_char == 0x2D && emergency_button_pressed_counter !=2) {	//hyphen-minus cannot be the first character of a profile
				four_second_timeout_counter = 0;
				is_typing = 0;
				uart_print("\r\nThe hyphen-minus cannot be the first character you enter!\r\n\r\nEnter your profile (integers: 0-9)->");
			}else if (buff[buff_index-1] == 0x2D && ((rx_char >= 0x30 && rx_char <=0x39) || rx_char == 0x2D) && emergency_button_pressed_counter !=2) {	//hyphen-minus must be the last character of the profile
				four_second_timeout_counter = 0;
				is_typing = 0;
				buff_index = 0;
				uart_print("\r\nThe hyphen-minus must be the last character of the profile!\r\n\r\nEnter your profile (integers: 0-9)->");
			}else {	//character is added to the main buffer
				buff[buff_index++] = (char)rx_char;
				uart_tx(rx_char);
			}
		}	while(rx_char != '\r' && buff_index < BUFF_SIZE);	//the do-while loop is executed until the user pressed enter, or a buffer overflow occures
		
			if (rx_char == 0x0D) {
				if (buff_index == 1 && emergency_button_pressed_counter == 0) {	//special text is printed, if used has not entered any characters, and is in normal state
					buff[buff_index-1] = '\0';
					is_typing = 0;
					four_second_timeout_counter = 0;
					uart_print("\r\nYou have not entered any characters!\r\n");
				}else if(buff_index != 1 && emergency_button_pressed_counter == 0) {	//acts done if there are characters in the buffer and the user is in noraml state
					buff[buff_index-1] = '\0';
					is_typing = 0;
					four_second_timeout_counter = 0;
					
					profile_running = 0;	//by setting this bool variable to 0, we basicly tell the timer_isr to stop executing the previous profile, because a new one is being processed for execution
					gpio_set(PA_5, 0);
					strncpy((char*)timer_buff, buff, BUFF_SIZE);	//copy the new profile (that exists in the main buff), to the timer_buf, so timer_isr can act upon the new characters of the profile
					current_digit_index = 0;
					digit_active_window_counter = 0;
					led_toggle_phase_counter = 0;
					profile_running = 1;	//by setting this bool variable to 1, (having copied the new profile to the timer_buf) the timer_isr, starts acting upon the new profile
					uart_print("\r\nNew profile started!\r\n");
				}else if (emergency_button_pressed_counter == 2) {	//if program is in override requested state...
					buff[buff_index-1] = '\0';
					if (strcmp(buff, "unlock") == 0) {	//compair the string that user entered, with the "unlock" password
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
		
		if (buff_index >= BUFF_SIZE) {	//uart_print in case of main buffer overflow.
			uart_print("\r\nStop trying to overflow my buffer! I resent that!\r\n");
		}
	}
}
