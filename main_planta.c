/******************************************************************************/
/*                                                                            */
/*  Description: Main correspondiente a la placa Planta                       */ 
/*  Author: Luis Arjona y Javier Trivi�o                                      */                           
/*                                                                            */
/******************************************************************************/

#include "main_planta.h"

#include <p30f4011.h>
#include <salvo.h>

#include "common.h"
#include "libLEDs.h"
#include "libTIMER.h"
#include "libLCD.h"
#include "libCAD.h"
#include "libKEYB.h"
#include "libCAN.h"


/******************************************************************************/
/* Salvo elements declarations                                                */
/******************************************************************************/

// Tasks TCBs for PLANTA
#define TASK_TENSION_MONITOR_P 		OSTCBP(1) /* task #1 */
#define TASK_GLYCEMIA_MONITOR_P 	OSTCBP(2) /* task #2 */
#define TASK_PACIENT_STATUS_P       OSTCBP(3) /* task #3 */
#define TASK_SHOW_OUTPUT_P          OSTCBP(4) /* task #4 */

// Tasks priorities for PLANTA
#define PRIO_TENSION_MONITOR            0
#define PRIO_GLYCEMIA_MONITOR           0
#define PRIO_PACIENT_STATUS             0
#define PRIO_SHOW_OUTPUT                0

// OS events control blocks (number of OS EVENT)
// Recall that the number of OS event must range from 1 to OSEVENTS (defined in salvocfg.h)
#define MSG_FOR_SHOW_OUTPUT            OSECBP(2)
#define EFLAG_FOR_PACIENT_STATUS       OSECBP(3)
#define EFLAG_FOR_PACIENT_STATUS_EFCB  OSEFCBP(1)

#define FLAG_TENSION                        0b0001
#define FLAG_GLYCEMIA                       0b0010
#define FLAG_TEMPERATURE_AND_OXYGEN_SAT     0b0100


/******************************************************************************/
/* Global Variable and macros declaration                                     */
/******************************************************************************/

#define CAD_INTERRUPT_PRIO                  1
#define iniValueEventShowPacientStatus      0x0
#define maskEventForPacientStatus           0xF

#define TENSION_LED         0
#define GLYCEMIA_LED        2
#define TEMPERATURE_LED     1
#define OXYGEN_SAT_LED      3

#define TENSION_KEY         0
#define GLYCEMIA_KEY        3
#define TEMPERATURE_KEY     2
#define OXYGEN_SAT_KEY      5

static unsigned int cad_value, tension, glycemia, temperature, oxygen_sat;
static unsigned char glycemia_monitor_activated = 1,
        tension_monitor_activated = 1,
        temperature_monitor_activated = 1,
        oxygen_sat_monitor_activated = 1;
static int GLYCEMIA_LOWER_TRESHOLD = 0;
static int GLYCEMIA_UPPER_TRESHOLD = 0;
static int TEMPERATURE_LOWER_TRESHOLD = 0;
static int TEMPERATURE_UPPER_TRESHOLD = 0;
static int TENSION_LOWER_TRESHOLD = 0;
static int TENSION_UPPER_TRESHOLD = 0;
static int OXYGEN_SAT_LOWER_TRESHOLD = 0;
static int OXYGEN_SAT_UPPER_TRESHOLD = 0;


/******************************************************************************/
/* Procedures declaration                                                     */
/******************************************************************************/
char alarmTresholdReached(pacient_status_t* pacientStatusP);
void activateAlarm(void);
void deactivateAlarm(void);

/******************************************************************************/
/* TASKS declaration and implementation for PLANTA                            */
/******************************************************************************/

 /* Task states

	DESTROYED / uninitalized
	STOPPED
	WAITING
	DELAYED
	ELIGIBLE /READY
	RUNNING

*/

void TaskTensionMonitor(void){
	while(1){
        if(getKeyNotBlocking() == TENSION_KEY){
            tension_monitor_activated = !tension_monitor_activated;
            toggleLED(TENSION_LED);
            
        }
        if(tension_monitor_activated){ // TODO en vez de este if, deshabilitar la tarea
            tension = (cad_value / 8) + 25;
            OSSetEFlag(EFLAG_FOR_PACIENT_STATUS, FLAG_TENSION);
        }
        OS_Delay(MONITORS_SAMPLING_PERIOD);
	}
}

void TaskGlycemiaMonitor(void){
	while(1){
        if(getKeyNotBlocking() == GLYCEMIA_KEY){
            glycemia_monitor_activated = !glycemia_monitor_activated;
            toggleLED(GLYCEMIA_LED);
        }
        if(glycemia_monitor_activated){ 
            glycemia = ((cad_value % 40) + 70) + (rand() % 10);
            OSSetEFlag(EFLAG_FOR_PACIENT_STATUS, FLAG_GLYCEMIA);
        }
		OS_Delay(MONITORS_SAMPLING_PERIOD);
	}
}

void TaskPacientStatus(void){
    static pacient_status_t pacientStatus;
    char eFlag;
    
    while(1){
        OSClrEFlag(EFLAG_FOR_PACIENT_STATUS, maskEventForPacientStatus);
        OS_WaitEFlag(EFLAG_FOR_PACIENT_STATUS, maskEventForPacientStatus, OSANY_BITS, OSNO_TIMEOUT);
        eFlag = OSReadEFlag(EFLAG_FOR_PACIENT_STATUS);
		if (eFlag & FLAG_TENSION)   pacientStatus.tension = tension;
		if (eFlag & FLAG_GLYCEMIA)  pacientStatus.glycemia = glycemia;
        if (eFlag & FLAG_TEMPERATURE_AND_OXYGEN_SAT){
            pacientStatus.temperature = temperature;
            pacientStatus.oxygen_sat = oxygen_sat;
        }
        
        OSSignalMsg(MSG_FOR_SHOW_OUTPUT, (OStypeMsgP) &pacientStatus);
        // enviar CAN
    }
}

void TaskShowOutput(void){
    static char msg[20];
    OStypeMsgP msgP;
	pacient_status_t *pacientStatusP;
    while(1){
        OS_WaitMsg(MSG_FOR_SHOW_OUTPUT, &msgP, OSNO_TIMEOUT);
        pacientStatusP = (pacient_status_t *) msgP;

        if(alarmTresholdReached(pacientStatusP)){
            activateAlarm();
        } else {
            deactivateAlarm();
        }
        
        LCDClear(); // Si no hay monitores, este clear no se activa, hay que crear otro flag
        if(tension_monitor_activated){
            sprintf(msg, "t:%dmmHg", pacientStatusP->tension);
            LCDMoveHome();
            LCDMoveFirstLine();
            LCDPrint(msg);
        }
        if(glycemia_monitor_activated){
            sprintf(msg, "G:%dg/ml", pacientStatusP->glycemia);
            LCDMoveHome();
            LCDMoveSecondLine();
            LCDPrint(msg);
        }
        if(temperature_monitor_activated){
            sprintf(msg, "T:%d�C", pacientStatusP->temperature);
            LCDMoveHome();
            LCDMoveFirstLine();
            int i;
            for (i = 0; i < 10; i++) {
                LCDMoveRight();
            }
            LCDPrint(msg);
        }
        if(oxygen_sat_monitor_activated){
            sprintf(msg, "Ox:%d%%", pacientStatusP->oxygen_sat);
            LCDMoveHome();
            LCDMoveSecondLine();
            int i;
            for (i = 0; i < 10; i++) {
                LCDMoveRight();
            }
            LCDPrint(msg);
        }
    }
}


/******************************************************************************/
/* Interrupts                                                                 */
/******************************************************************************/

inline void planta_ISR_T1Interrupt(void){
    TimerClearInt();
	OSTimer();
}

inline void planta_ISR_ADCInterrupt(void){
	// Get value from CAD
	cad_value = CADGetValue();

	// Clear interrupt
	IFS0bits.ADIF = 0;
}

inline void planta_ISR_C1Interrupt(void){
	unsigned int rxMsgSID;
	unsigned char rxMsgData[8];
	unsigned char rxMsgDLC;

	// Clear CAN global interrupt
	CANclearGlobalInt();

	if (CANrxInt()){
		// Clear RX interrupt
		CANclearRxInt ();

		// Read SID, DLC and DATA
		rxMsgSID = CANreadRxMessageSID();
		rxMsgDLC = CANreadRxMessageDLC();
		CANreadRxMessageDATA (rxMsgData);

		// Clear RX buffer
		CANclearRxBuffer();

		// Process data
        switch(rxMsgSID){
            toggleLED(5);
            case EXTERNAL_MONITORS_DATA_SID:
                temperature = ((pacient_status_t*) rxMsgData)->temperature;
                oxygen_sat = ((pacient_status_t*) rxMsgData)->oxygen_sat;
                OSSetEFlag(EFLAG_FOR_PACIENT_STATUS, FLAG_TEMPERATURE_AND_OXYGEN_SAT);
                break;
        }
	}
}

/******************************************************************************/
/* Procedures implementation                                                  */
/******************************************************************************/

char alarmTresholdReached(pacient_status_t* pacientStatusP){
    if(pacientStatusP->glycemia < GLYCEMIA_LOWER_TRESHOLD
            || pacientStatusP->glycemia > GLYCEMIA_UPPER_TRESHOLD){
        return 1;
        
    } else if(pacientStatusP->temperature < TEMPERATURE_LOWER_TRESHOLD
            || pacientStatusP->temperature > TEMPERATURE_UPPER_TRESHOLD){
        return 1;
        
    } else if(pacientStatusP->tension < TENSION_LOWER_TRESHOLD
            || pacientStatusP->tension > TENSION_UPPER_TRESHOLD){
        return 1;
        
    } else if(pacientStatusP->oxygen_sat < OXYGEN_SAT_LOWER_TRESHOLD
            || pacientStatusP->oxygen_sat > OXYGEN_SAT_UPPER_TRESHOLD){
        return 1;
        
    } else {
        return 0;
        
    }
}

void activateAlarm(void){
    onLED(4);
}

void deactivateAlarm(void){
    offLED(4);
}

void main_planta(void){
	// ===================
	// Init peripherals
	// ===================
	CADInit(CAD_INTERACTION_BY_INTERRUPT, CAD_INTERRUPT_PRIO);
	CADStart(CAD_INTERACTION_BY_INTERRUPT);
    
	// =========================
	// Create Salvo structures
	// =========================
	OSInit();

	// Create tasks (name, tcb, priority) and push them to ELIGIBLE STATE
	// From 1 up to OSTASKS tcbs available
	// Priorities from 0 (highest) down to 15 (lowest)
	OSCreateTask(TaskTensionMonitor, TASK_TENSION_MONITOR_P, PRIO_TENSION_MONITOR);
    OSCreateTask(TaskGlycemiaMonitor, TASK_GLYCEMIA_MONITOR_P, PRIO_GLYCEMIA_MONITOR);
    OSCreateTask(TaskPacientStatus, TASK_PACIENT_STATUS_P, PRIO_PACIENT_STATUS);
    OSCreateTask(TaskShowOutput, TASK_SHOW_OUTPUT_P, PRIO_SHOW_OUTPUT);
    
    // Create event flag (ecbP, efcbP, initial value)
	OSCreateEFlag(EFLAG_FOR_PACIENT_STATUS, EFLAG_FOR_PACIENT_STATUS_EFCB, iniValueEventShowPacientStatus);
    
    // Create mailbox
	OSCreateMsg(MSG_FOR_SHOW_OUTPUT, (OStypeMsgP) 0);

	// =============================================
	// Enable peripherals that trigger interrupts
	// =============================================
	Timer1Init(TIMER_PERIOD_FOR_125ms, TIMER_PSCALER_FOR_125ms, 4);
	Timer1Start();

	// =============================================
	// Enter multitasking environment
	// =============================================
	while(1){
        OSSched();
	}
}
