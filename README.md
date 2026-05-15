# STM32 Secure Profile Controller

An **Event-Driven Firmware** for the STM32 (Nucleo-F411RE) microcontroller that implements an LED profile control system with an emphasis on safety and high responsiveness.

## Description
This project implements a controller that receives sequences of digits (profiles) via UART. Each digit corresponds to a specific blinking frequency for the User LED (LD2). The system integrates an **Emergency Stop (E-Stop)** function via external interrupt (EXTI), which forces the device into a locked state.

## Technical Features
* **Architecture:** Event-Driven utilizing Interrupt Service Routines (ISRs).
* **Concurrency:** Complete decoupling of data acquisition (UART ISR) from processing (Main Thread) using a queue.
* **Timing:** Utilization of the internal **SysTick Timer** (10ms tick) for managing multiple timeouts, flags and LED toggling.
* **Power Management:** Implementation of the `__WFI()` (Wait For Interrupt) instruction to enter low-power sleep mode during idle periods.
* **Security & Safety:** * Instantaneous Emergency Stop.
    * Protection against Buffer Overflow.
    * Timed Override Mode with password authentication for system recovery.

## Code Structure
* `main.c`: The central orchestrator and system State Machine.
* `uart_rx_isr`: Manages character reception and real-time input filtering.
* `timer_isr`: Responsible for LED toggling and system timeouts.
* `p_sw_pressed_isr`: Critical interrupt for Emergency Stop management.

## Installation & Usage
1. Open the project in your preferred IDE (e.g., Keil uVision or STM32CubeIDE).
2. Connect your Nucleo-F411RE board.
3. Build and Flash the code to the microcontroller.
4. Open a Serial Terminal (e.g., Tera Term) configured at **115200 baud**.

## Operating Instructions
1. **Profile Input**: Type a sequence of digits (e.g., `1234`) and press `Enter`. The LED will begin blinking at the corresponding frequencies.
2. **Emergency Stop**: Press the blue button (B1) on the board. The system locks instantly, and the LED remains solid.
3. **Unlock**: Press the blue button again. You have 5 seconds to type the password `unlock` to restore the system to its normal state.

## Academic Context
This project was developed as part of the **"Microprocessors and Peripherals"** course at the Department of Electrical and Computer Engineering, April 2026.