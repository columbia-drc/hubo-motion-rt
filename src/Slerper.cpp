

#include "Slerper.h"

using namespace RobotKin;
using namespace std;

double sign(double x)
{
    return (x < 0) ? -1 : (x > 0);
}

Slerper::Slerper() :
    kin()
{
#ifdef HAVE_REFLEX
    aa_mem_region_init(&reg, 1024*32);
#endif //HAVE_REFLEX

    kin.armConstraints.maxAttempts = 2;
    
    nomSpeed = 0.15;
    nomAcc   = 0.1;

    nomRotSpeed = 0.2; // TODO play with these
    nomRotAcc   = 0.1;
    
    for(int i=0; i<2; i++)
    {
        dr[i] = TRANSLATION::Zero();
        V[i] = TRANSLATION::Zero();
        dV[i] = TRANSLATION::Zero();
        
        angle[i] = 0;
        W[i] = 0;
        dW[i] = 0;
    }

    worstOffender = -2;
    worstOffense = 0;
    
    
    limb[RIGHT] = "RightArm";
    limb[LEFT]  = "LeftArm";

    dump = fopen("output-waypoints", "a+");    

}

#ifdef HAVE_REFLEX
void Slerper::commenceSlerping(int side, hubo_manip_cmd_t &cmd)
{
    std::cerr << "We do not currently have reflex/amino support" << std::endl;
    
    return;
    
    hubo.update();
    kin.updateHubo(hubo);

    aa_mem_region_release(&reg);

    rfx_trajx_parablend_t T;
    rfx_trajx_splend_init(&T, &reg, 1);

    rfx_trajx_t *pT = (rfx_trajx_t*)&T;

    // NOTE: Amino quaternions are x, y, z, w

    double X[3];

    double R[4];


}

#else //HAVE_REFLEX
void Slerper::commenceSlerping(int side, hubo_manip_cmd_t &cmd, Hubo_Control &hubo, double dt)
{
    bool verbose = true;
//    bool verbose = false;
/*
if(worstOffender == -2)
{
    worstOffender = -1;
    hubo.getLeftArmAngles(armAngles[LEFT]);
}

if(verbose)
{
    kin.updateHubo(hubo, true);
    cout << "State Error: " << (next.translation() - kin.linkage("LeftArm").tool().respectToRobot().translation()).transpose() << "\t||\t";
    ArmVector final;
    hubo.getLeftArmAngles(final);
    for(int w=0; w < 7; w++)
    {
        if( fabs(final(w)-armAngles[LEFT](w)) > worstOffense )
        {
            worstOffense = final(w)-armAngles[LEFT][w];
            worstOffender = w;
        }
    }
    cout << "WO: " << worstOffender << " (" << worstOffense << ")\t||\t";
}
*/




    kin.updateHubo(hubo, false);
    
    bool dual;
    if(cmd.m_mode[side] == MC_DUAL_TELEOP)
        dual = true;
    else
        dual = false;
    
    
    int alt;
    if(side==LEFT)
        alt = RIGHT;
    else
        alt = LEFT;

    ArmVector nomJointSpeed, nomJointAcc; nomJointSpeed.setOnes(); nomJointAcc.setOnes();
    nomJointSpeed = 2.0*nomJointSpeed;
    nomJointAcc   = 10.0*nomJointAcc;

    hubo.setArmNomSpeeds(side, nomJointSpeed);
    hubo.setArmNomAcc(side, nomJointAcc);

    if(dual)
    {
        hubo.setArmNomSpeeds(alt, nomJointSpeed);
        hubo.setArmNomAcc(side, nomJointAcc);
    }

    RobotKin::TRANSFORM toolTf = RobotKin::TRANSFORM::Identity();
    toolTf.translate( Vector3d(cmd.m_tool[side].t_pose.x,
                               cmd.m_tool[side].t_pose.y,
                               cmd.m_tool[side].t_pose.z) );
    toolTf.rotate( Eigen::Quaterniond(cmd.m_tool[side].t_pose.w,
                                      cmd.m_tool[side].t_pose.i,
                                      cmd.m_tool[side].t_pose.j,
                                      cmd.m_tool[side].t_pose.k) );

    kin.setTool(side, toolTf);

    if(dual)
    {
        RobotKin::TRANSFORM altTf = RobotKin::TRANSFORM::Identity();
        altTf.translate( Vector3d(cmd.dual_offset.x,
                                  cmd.dual_offset.y,
                                  cmd.dual_offset.z) );
        altTf.rotate( Eigen::Quaterniond(cmd.dual_offset.w,
                                  cmd.dual_offset.i,
                                  cmd.dual_offset.j,
                                  cmd.dual_offset.k) );
        kin.setTool(alt, altTf);
    }
    

    if(cmd.m_frame[side] == MC_GLOBAL)
        start = kin.linkage(limb[side]).tool().withRespectTo(kin.linkage("RightLeg").tool());
    else if(cmd.m_frame[side] == MC_ROBOT)
        start = kin.linkage(limb[side]).tool().respectToRobot();

    next = TRANSFORM::Identity();

    
    goal = TRANSFORM::Identity();
    goal.translate(TRANSLATION(cmd.pose[side].x, 
                               cmd.pose[side].y,
                               cmd.pose[side].z));
    goal.rotate(Eigen::Quaterniond(cmd.pose[side].w,
                            cmd.pose[side].i,
                            cmd.pose[side].j,
                            cmd.pose[side].k));

    
    J = kin.armJacobian(side);

    hubo.getArmRefVels(side, qdotC);
//    hubo.getArmAngles(side, qdotC);
//    qdotC = (qdotC - lastAngles[side])/dt;
    for(int i=0; i<7; i++)
        qdot(i) = qdotC(i);

    mscrew = J*qdot;

    for(int i=0; i<3; i++)
        V[side](i) = mscrew(i);

    
    dr[side] = goal.translation() - start.translation();


if(verbose)
{
    std::cout << "Remaining: " << (goal.translation()-start.translation()).transpose() << "\t||\t";
    std::cout << "Vel : " << V[side].transpose() << "\t||\t";
}
/*    
    dV[side] = dr[side].normalized()*fabs(nomSpeed) - V[side];

    
    adr = sqrt(fabs(2.0*nomAcc*dr[side].norm()));
    if( V[side].norm() >= adr )
        dV[side] = dr[side].normalized()*adr - V[side];
    else
        clampMag(dV[side], fabs(nomAcc*dt));
    
    V[side] += dV[side];
    
    
    if( dr[side].norm() > V[side].norm()*dt || dr[side].dot(V[side]) < 0 )
        dr[side] = V[side]*dt;

    
    V[side] = dr[side]/dt;
*/    
    dV[side] = dr[side]/dt;
    stopSpeed = sqrt(2*nomAcc*dr[side].norm());
    maxVel = std::min(nomSpeed, stopSpeed);
    
    if( dV[side].norm() > maxVel )
        dV[side] *= maxVel/dV[side].norm();
    
    accel = (dV[side]-V[side])/dt;
    if( accel.norm() > nomAcc )
        accel *= nomAcc / accel.norm();

    V[side] += accel*dt;

    if( dr[side].norm() > V[side].norm()*dt || dr[side].dot(V[side]) < 0 )
        dr[side] = V[side]*dt;
    
    
    next.translate(dr[side]);
    next.translate(start.translation());


    angax = goal.rotation()*start.rotation().transpose();
    
    angle[side] = angax.angle();

    
    if( angle[side] > M_PI )
        angle[side] = angle[side]-2*M_PI;
    
    dW[side] = sign(angle[side])*nomRotSpeed - W[side];
    
    ada = sqrt(fabs(2.0*nomRotAcc*angle[side]));
    if( fabs(W[side]) >= ada )
        dW[side] = sign(angle[side])*ada - W[side];
    else if( dW[side] > fabs(nomRotAcc*dt) )
        dW[side] = fabs(nomRotAcc*dt);
    else if( dW[side] < -fabs(nomRotAcc*dt) )
        dW[side] = -fabs(nomRotAcc*dt);
    
    W[side] += dW[side];
    
    if( fabs(angle[side]) > fabs(W[side]*dt) || angle[side]*W[side] < 0 )
        angle[side] = W[side]*dt;
    
    W[side] = angle[side]/dt;
    
    next.rotate(Eigen::AngleAxisd(angle[side], angax.axis()));
    next.rotate(start.rotation());


if(verbose)
{

    fprintf(dump, "%f\t%f\t%f\t%f\t", hubo.getTime(),
                    next.translation().x(),
                    next.translation().y(),
                    next.translation().z() );

    fprintf(dump, "%f\t%f\t%f\t",
                    V[LEFT](0), V[LEFT](1), V[LEFT](2));

    fprintf(dump, "%f\t%f\t\t%f\t",
                    dV[LEFT](0), dV[LEFT](1), dV[LEFT](2));

    ArmVector final;
    hubo.getLeftArmAngles(final);
    
    for(int j=0; j<7; j++)
        fprintf(dump, "%f\t", armAngles[LEFT](j)-final(j));

    fprintf(dump, "\n");
    fflush(dump);

}


    if(cmd.m_frame[side] == MC_GLOBAL)
        next = kin.linkage("RightLeg").tool().respectToRobot()*next;


    
    hubo.getArmAngles(side, armAngles[side]);
    if(dual)
        hubo.getArmAngles(alt, armAngles[alt]);
    
    lastAngles[side] = armAngles[side];
    if(dual)
        lastAngles[alt] = armAngles[alt];
    
    rk_result_t result = kin.armIK(side, armAngles[side], next);
    if(dual)
        kin.armIK(alt, armAngles[alt], next);



    if( result != RK_SOLVED )
        cout << rk_result_to_string(result) << " "; fflush(stdout);

if(verbose)
{
    cout << "Error: " << (next.translation() - kin.linkage("LeftArm").tool().respectToRobot().translation()).transpose() << endl;
}

//if(verbose)
if(false)
{
    cout     << "EE:  " << endl << next.matrix() << endl << endl
             << "wrt Robot: " << endl << kin.linkage("RightArm").tool().respectToRobot().matrix() << endl << endl
//             << "wrt Foot : " << endl << kin.linkage("RightArm").tool().withRespectTo(kin.joint("RAP")).matrix() << endl << endl;
             << "wrt Foot : " << endl << kin.linkage("RightArm").tool().withRespectTo(kin.linkage("RightLeg").tool()).matrix() << endl << endl;

    cout  << "Angles: "  << armAngles[side].transpose() << endl
          << "Last:   "  << lastAngles[side].transpose() << endl
          << "Vels: " << (armAngles[side]-lastAngles[side]).transpose()/dt << endl;
}

    hubo.setArmAngles(side, armAngles[side]);

    if(dual)
        hubo.setArmAngles(alt, armAngles[alt]);

}
#endif //HAVE_REFLEX




