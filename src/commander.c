#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_gamecontroller.h>
#include <sys/time.h>
#include <linux/can.h>

#include "oscc.h"
#include "vehicles.h"

#include "can_protocols/brake_can_protocol.h"
#include "can_protocols/steering_can_protocol.h"
#include "can_protocols/throttle_can_protocol.h"
#include "can_protocols/fault_can_protocol.h"

#include "joystick.h"


#define CONSTRAIN(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

#define JOYSTICK_AXIS_THROTTLE (SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
#define JOYSTICK_AXIS_BRAKE (SDL_CONTROLLER_AXIS_TRIGGERLEFT)
#define JOYSTICK_AXIS_STEER (SDL_CONTROLLER_AXIS_LEFTX)
#define JOYSTICK_BUTTON_ENABLE_CONTROLS (SDL_CONTROLLER_BUTTON_START)
#define JOYSTICK_BUTTON_DISABLE_CONTROLS (SDL_CONTROLLER_BUTTON_BACK)
#define BRAKES_ENABLED_MIN (0.05)
#define JOYSTICK_DELAY_INTERVAL (50000)
#define COMMANDER_ENABLED ( 1 )
#define COMMANDER_DISABLED ( 0 )
#define BRAKE_FILTER_FACTOR (0.2)
#define THROTTLE_FILTER_FACTOR (0.2)
#define STEERING_FILTER_FACTOR (0.1)

static int commander_enabled = COMMANDER_DISABLED;

static bool control_enabled = false;

static double curr_angle;

static int get_normalized_position( unsigned long axis_index, double * const normalized_position );
static int check_trigger_positions( );
static int commander_disable_controls( );
static int commander_enable_controls( );
static int get_button( unsigned long button, unsigned int* const state );
static int command_brakes( );
static int command_throttle( );
static int command_steering( );
static void brake_callback(oscc_brake_report_s *report);
static void throttle_callback(oscc_throttle_report_s *report);
static void steering_callback(oscc_steering_report_s *report);
static void fault_callback(oscc_fault_report_s *report);
static void obd_callback(struct can_frame *frame);
static double calc_exponential_average( double average,
                                        double setpoint,
                                        double factor );

int commander_init( int channel )
{
    int return_code = OSCC_ERROR;

    if ( commander_enabled == COMMANDER_DISABLED )
    {
        commander_enabled = COMMANDER_ENABLED;

        return_code = oscc_open( channel );

        if ( return_code != OSCC_ERROR )
        {
            // register callback handlers
            oscc_subscribe_to_obd_messages(obd_callback);
            oscc_subscribe_to_brake_reports(brake_callback);
            oscc_subscribe_to_steering_reports(steering_callback);
            oscc_subscribe_to_throttle_reports(throttle_callback);
            oscc_subscribe_to_fault_reports(fault_callback);

            return_code = joystick_init( );

            printf( "Waiting for joystick controls to zero\n" );

            while ( return_code != OSCC_ERROR )
            {
                return_code = check_trigger_positions( );

                if ( return_code == OSCC_WARNING )
                {
                    (void) usleep( JOYSTICK_DELAY_INTERVAL );
                }
                else if ( return_code == OSCC_ERROR )
                {
                    printf( "Failed to wait for joystick to zero the control values\n" );
                }
                else
                {
                    printf( "Joystick controls successfully initialized\n" );

                    break;
                }
            }
        }
    }
    return ( return_code );
}

void commander_close( int channel )
{
    if ( commander_enabled == COMMANDER_ENABLED )
    {
        commander_disable_controls( );

        oscc_disable( );

        oscc_close( channel );

        joystick_close( );

        commander_enabled = COMMANDER_DISABLED;
    }
}

int check_for_controller_update( )
{
    static unsigned int disable_button_previous = 0;
    unsigned int disable_button_current = 0;

    int return_code = joystick_update( );

    if ( return_code == OSCC_OK )
    {
        return_code = get_button( JOYSTICK_BUTTON_DISABLE_CONTROLS,
                                  &disable_button_current );
    }

    if ( return_code == OSCC_OK )
    {
        if ( (disable_button_previous != 1)
            && (disable_button_current != 0 ) )
        {
            return_code = commander_disable_controls( );
        }

        disable_button_previous = disable_button_current;
    }

    static unsigned int enable_button_previous = 0;
    unsigned int enable_button_current = 0;

    if ( return_code == OSCC_OK )
    {
            return_code = get_button( JOYSTICK_BUTTON_ENABLE_CONTROLS,
                                      &enable_button_current );

            if ( return_code == OSCC_OK )
            {
                if ( (enable_button_previous != 1)
                    && (enable_button_current != 0 ) )
                {
                    return_code = commander_enable_controls( );
                }

                enable_button_previous = enable_button_current;
            }
    }

    if ( control_enabled )
    {
        return_code = command_brakes( );

        if ( return_code == OSCC_OK )
        {
            return_code = command_throttle( );
        }

        if ( return_code == OSCC_OK )
        {
            return_code = command_steering( );
        }
    }

    return return_code;
}

static int get_normalized_position( unsigned long axis_index, double * const normalized_position )
{
    int return_code = OSCC_ERROR;

    int axis_position = 0;

    return_code = joystick_get_axis( axis_index, &axis_position );

    if ( return_code == OSCC_OK )
    {
        if ( axis_index == JOYSTICK_AXIS_STEER )
        {
            ( *normalized_position ) = CONSTRAIN(
            ((double) axis_position) / INT16_MAX,
            -1.0,
            1.0);
        }
        else
        {
            ( *normalized_position ) = CONSTRAIN(
            ((double) axis_position) / INT16_MAX,
            0.0,
            1.0);
        }
    }

    return ( return_code );

}

static int check_trigger_positions( )
{
    int return_code = OSCC_ERROR;

    return_code = joystick_update( );


    double normalized_brake_position = 0;

    if ( return_code == OSCC_OK )
    {
        return_code = get_normalized_position( JOYSTICK_AXIS_BRAKE, &normalized_brake_position );
    }


    double normalized_throttle_position = 0;

    if ( return_code == OSCC_OK )
    {
        return_code = get_normalized_position( JOYSTICK_AXIS_THROTTLE, &normalized_throttle_position );
    }


    if ( return_code == OSCC_OK )
    {
        if ( ( normalized_throttle_position > 0.0 )
             || ( normalized_brake_position > 0.0 ) )
        {
            return_code = OSCC_WARNING;
        }
    }

    return return_code;
}

static int commander_disable_controls( )
{
    int return_code = OSCC_ERROR;

    if ( (commander_enabled == COMMANDER_ENABLED)
        && (control_enabled == true) )
    {
        printf( "Disable controls\n" );

        return_code = oscc_disable();

        if ( return_code == OSCC_OK )
        {
            control_enabled = false;
        }
    }
    else
    {
        return_code = OSCC_OK;
    }

    return return_code;
}

static int commander_enable_controls( )
{
    int return_code = OSCC_ERROR;

    if ( (commander_enabled == COMMANDER_ENABLED)
        && (control_enabled == false) )
    {
        printf( "Enable controls\n" );

        return_code = oscc_enable();

        if ( return_code == OSCC_OK )
        {
            control_enabled = true;
        }
    }
    else
    {
        return_code = OSCC_OK;
    }

    return ( return_code );
}

static int get_button( unsigned long button, unsigned int* const state )
{
    int return_code = OSCC_ERROR;

    if ( state != NULL )
    {
        unsigned int button_state;

        return_code = joystick_get_button( button, &button_state );

        if ( ( return_code == OSCC_OK ) &&
             ( button_state == JOYSTICK_BUTTON_STATE_PRESSED ) )
        {
            ( *state ) = 1;
        }
        else
        {
            ( *state ) = 0;
        }
    }

    return ( return_code );
}

// Since the OSCC API requires a normalized value, we will read in and
// normalize a value from the game pad, using that as our requested brake position.
static int command_brakes( )
{
    int return_code = OSCC_ERROR;

    static double average = 0.0;

    if ( commander_enabled == COMMANDER_ENABLED && control_enabled == true )
    {
        double normalized_position = 0;

        return_code = get_normalized_position( JOYSTICK_AXIS_BRAKE, &normalized_position );

        if ( return_code == OSCC_OK && normalized_position >= 0.0 )
        {
            average = calc_exponential_average(
                average,
                normalized_position,
                BRAKE_FILTER_FACTOR );

            printf("Brake: %f ", average);

            return_code = oscc_publish_brake_position( average );
        }
    }

    return ( return_code );
}

// For the throttle command, we want to send a normalized position based on the
// throttle position trigger. We also don't want to send throttle commands if
// we are currently braking.
static int command_throttle( )
{
    int return_code = OSCC_ERROR;

    static double average = 0.0;

    if ( commander_enabled == COMMANDER_ENABLED && control_enabled == true )
    {
        double normalized_throttle_position = 0;

        return_code = get_normalized_position( JOYSTICK_AXIS_THROTTLE, &normalized_throttle_position );

        if ( return_code == OSCC_OK && normalized_throttle_position >= 0.0 )
        {
            double normalized_brake_position = 0;

            // If braking, do not throttle
            return_code = get_normalized_position( JOYSTICK_AXIS_BRAKE, &normalized_brake_position );

            if ( normalized_brake_position >= BRAKES_ENABLED_MIN )
            {
                normalized_throttle_position = 0.0;
            }
        }

        if ( return_code == OSCC_OK && normalized_throttle_position >= 0.0 )
        {
            average = calc_exponential_average(
                average,
                normalized_throttle_position,
                THROTTLE_FILTER_FACTOR );

            printf("Throttle: %f ", average);

            return_code = oscc_publish_throttle_position( average );
        }
    }

    return ( return_code );
}

// To send the steering command, we first get the normalized axis position from
// the game controller. Since the car will fault if it detects too much discontinuity
// between spoofed output signals, we use an exponential average filter to smooth
// our output.
static int command_steering( )
{
    int return_code = OSCC_ERROR;

    static double average = 0.0;

    if ( commander_enabled == COMMANDER_ENABLED && control_enabled == true )
    {
        double normalized_position = 0;

        return_code = get_normalized_position( JOYSTICK_AXIS_STEER, &normalized_position );

        if( return_code == OSCC_OK )
        {
            average = calc_exponential_average(
                average,
                normalized_position,
                STEERING_FILTER_FACTOR);

            printf("Steering: %f\n", average);

            return_code = oscc_publish_steering_torque( average );
        }


    }
    return ( return_code );
}

/*
 * These callback functions just check the reports for operator overrides. The
 * firmware modules should have disabled themselves, but we will send the
 * command again just to be safe.
 *
 */
static void throttle_callback(oscc_throttle_report_s *report)
{
    if ( report->operator_override )
    {
        commander_disable_controls();

        printf("Override: Throttle\n");
    }
}

static void steering_callback(oscc_steering_report_s *report)
{
    if ( report->operator_override )
    {
        commander_disable_controls();

        printf("Override: Steering\n");
    }
}

static void brake_callback(oscc_brake_report_s * report)
{
    if ( report->operator_override )
    {
        commander_disable_controls();

        printf("Override: Brake\n");
    }
}

static void fault_callback(oscc_fault_report_s *report)
{
    commander_disable_controls();

    printf("Fault: ");

    if ( report->fault_origin_id == FAULT_ORIGIN_BRAKE )
    {
        printf("Brake\n");
    }
    else if ( report->fault_origin_id == FAULT_ORIGIN_STEERING )
    {
        printf("Steering\n");
    }
    else if ( report->fault_origin_id == FAULT_ORIGIN_THROTTLE )
    {
        printf("Throttle\n");
    }
}

// To cast specific OBD messages, you need to know the structure of the
// data fields and the CAN_ID.
static void obd_callback(struct can_frame *frame)
{
    if ( frame->can_id == KIA_SOUL_OBD_STEERING_WHEEL_ANGLE_CAN_ID )
    {
        kia_soul_obd_steering_wheel_angle_data_s * steering_data = (kia_soul_obd_steering_wheel_angle_data_s*) frame->data;

        curr_angle = steering_data->steering_wheel_angle * KIA_SOUL_OBD_STEERING_ANGLE_SCALAR;
    }
}

static double calc_exponential_average( double average,
                                        double setpoint,
                                        double factor )
{
    double exponential_average =
        ( setpoint * factor ) + ( ( 1.0 - factor ) * average );

    return ( exponential_average );
}
