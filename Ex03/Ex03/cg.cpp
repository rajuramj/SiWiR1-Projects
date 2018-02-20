//*************************************************************************************
//******* 2D POISSON's EQUATION USING MPI PARALLELISATION****************************//
//*************************************************************************************
/*
    In this Program , we have robust domain decomposition which divides full domain into subdomain
    in 1D linear fashion in y-direction ,Then cartesian toppology is set-up for easy communication 
    between  processor. Then residual array is calculated by rank 0 processor and then it is distributed
    among given number of processor . Memory allocation takes places according to their position in cartesian topology . 

    For cg iteration, each processor will calculate u,d,r and once the convergent criteria is satisfied, all proccessors will 
    send their local u's to 0th processor, which will then write the solution to solution.txt.

*/

//***************************
//*** INCLUDES ************//
//***************************

#include <iostream>
#include <cmath>
#include <mpi.h>
#include <vector>
#include <fstream>
#include "Timer.h"    

//***************************************************
//****** GLOBAL VARAIBLES *************************//
//*************************************************** 

const double PI = 3.14;
const double con_sinh = 267.744894041; // This is sinh(2*pi)

//**************************************************
//******* FUNCTION DECLARATIONS *******************//
//**************************************************

inline void domaindecompose(const size_t &numy,const int &size,int domain[]);
inline void Boundary_Init(const int &numx,const int &numy,const double hx,std::vector<double> &u);
inline void func_init(const int &numx,const int &numy,const double &hx,const double &hy,std::vector<double>& f);
inline double Compute_Matrix_Vec_Mult(const double &u_center,const double &u_left,const double &u_right,const double &u_up,const double &u_down,const double &constant,const double &hxsqinv,const double &hysqinv);
void calInnerGridIndexes(const int &numGridX,const int &numGridY,std::vector<int>& ind) ;
inline void write_sol(const std::vector<double> &u_temp,const double hx,const double hy,const int numx,const int numy);
bool terminate(const double value, const double tol);
inline double cal_Scalar_Product(const std::vector<double> &temp_r,const int size);


//*****************
//** MAIN()******//
//*****************
int main(int argc, char *argv[])
{
	int size(0);
	int rank(0);

	// *************************************
	// *** MPI Initialisation **************
	//**************************************
	MPI_Init(&argc,&argv);
    MPI_Comm_size( MPI_COMM_WORLD, &size );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

  	// Checking whether number of parameters passed during main call are as per the requirement or not and if 
  	// they are not , program is terminated and this is checked by processor 0 

  	
  		if (rank == 0 && argc!=5) {
   		std::cout << "Aborting the Program , Please check the number of the input arguments" << std::endl;
    	exit(1);
  	}
    
    if(rank ==0)
    std::cout<<"Conjucate Gradient Algorithm for 2D Poissson's Equation with MPI-Parallerisation"<<std::endl;
  	

    //***************************************
    //******Cartesian Cordinate System ****** 
    //***************************************

    MPI_Comm new_comm(MPI_COMM_NULL);
    const int ndims = 2;
    int dim[ndims]={1,size};
    int periods[ndims] = {0,0};
    int reorder = 1;
    int up,down = 0;
    int coords[2] ={0,0};
    int cart_rank(0);

    MPI_Cart_create(MPI_COMM_WORLD,ndims,dim,periods,reorder,&new_comm);
    MPI_Comm_rank(new_comm,&cart_rank);
    MPI_Cart_coords(new_comm,cart_rank,2,coords);
    MPI_Cart_shift(new_comm,1,1,&down,&up);


	//********************************************************************
	//****** PARAMETER INITIALISATION **********************************//
	//********************************************************************
	
    std::string s1 = argv[1],s2= argv[2],s3 = argv[3], s4 = argv[4];
  	const size_t numx_interval = std::stoi(s1) ;
  	const size_t numy_interval = std::stoi(s2);
  	const int num_iter = std::stoi(s3);
    const double tol = std::stod(s4);
    const double tolsq = tol*tol;
  	const int numgridpoints_x = numx_interval+1;
  	const size_t numgridpoints_y = numy_interval+1;
  	const double hx  = 2.0 / (numx_interval);
  	const double hy = 1.0 / (numy_interval);
  	const double hxsq = hx * hx ;
  	const double hysq = hy * hy ;
  	const double hxsqinv = 1.0/ hxsq;
  	const double hysqinv = 1.0/ hysq;
  	const double K = 2 * PI;
  	const double constant = (2*hxsqinv) + (2*hysqinv) + (K*K);
    int sizeproc=0;
  	int domain[5] ={0};
    int new_x=0;
    int new_y=0;
    double delta_0=0.;
    double scalar_Product_d_z = 0.,global_scalar_Product_d_z=0.;
    double scalar_Product_r_r = 0.,delta_1=0.;
    std::vector<int> proc_ind;
    double alpha, beta;
    int isSolutionConverged = 0;
    MPI_Status status;

    
  	//***************************************
  	//** Domain Decompose *****************//
  	//***************************************
  	if (rank==0)
  	{
		  domaindecompose(numgridpoints_y,size,domain);

  	}

    // Memory Allocation
   

    //********************************************
    // Allocating memory in each processor *****//
    //********************************************
    // Memory in each processor allocated according to domain array 
    
    // Communicate 

    if(cart_rank == 0)
    {
       if (domain[2]==0)
      {
        sizeproc = domain[1] * numgridpoints_x;
        for (auto i = 1; i < size; ++i)
        {
          MPI_Send(&sizeproc,1,MPI_INT,i,0,new_comm);
        }
      }
      else
      {
        sizeproc= domain[1] * numgridpoints_x;
        int sizeproc_2= domain[3] * numgridpoints_x;
        for (auto i = 1; i < size-1; ++i)
        {
          MPI_Send(&sizeproc,1,MPI_INT,i,0,new_comm);
        }
          MPI_Send(&sizeproc_2,1,MPI_INT,size-1,0,new_comm);
      }
    }
    else
    {
      
      MPI_Recv(&sizeproc,1,MPI_INT,0,0,new_comm,&status);
     
    }

    MPI_Barrier( MPI_COMM_WORLD );
    

    std::vector<double> u(numgridpoints_x*numgridpoints_y,0.0);
    std::vector<double> d(2*numgridpoints_x + sizeproc,0.0);
    std::vector<double> z(2*numgridpoints_x + sizeproc,0.0);
    std::vector<double> r(numgridpoints_x*numgridpoints_y,0.0);

    MPI_Barrier( MPI_COMM_WORLD );


    //***********************************************
    //**** Calculation of Residual r by rank 0  ***//
    //***********************************************
    if(cart_rank==0)
    {

      Boundary_Init(numgridpoints_x,numgridpoints_y,hx,u);
    

      //**********************************************************************
      //Forcing Conditins
      //**********************************************************************
        std::vector<double> f(numgridpoints_x*numgridpoints_y,0.0);
        func_init(numgridpoints_x, numgridpoints_y,hx,hy,f);
      

      //**********************************************************************
      //Correct Index Calculation
      //**********************************************************************
        std::vector<int> ind;
        calInnerGridIndexes(numgridpoints_x,numgridpoints_y,ind);
      
      
      // Computing the residual by rank 0 ;

      for (unsigned int i = 0; i < ind.size(); i++) {
        double u_left= u[ind[i]-1];
        double u_right= u[ind[i]+1];
        double u_up= u[ind[i]+numgridpoints_x];
        double u_down = u[ind[i]-numgridpoints_x];
        double u_center= u[ind[i]];
        r[ind[i]]= f[ind[i]] - Compute_Matrix_Vec_Mult(u_center,u_left,u_right,u_up,u_down,constant,hxsqinv,hysqinv);
      }
      delta_0=cal_Scalar_Product(r,numgridpoints_x*numgridpoints_y);
    
  	
    //******************************************************
    //*** Distribution of r and u into several processors ********
    //****************************************************** 

    //***************************************************************
    // Distribution of r and u if domain is decomposed in first form ******z
  
    if(domain[4]==1)
    {
      for (auto i = 1; i < size; ++i)
      {
        /* Sending r and u */
        MPI_Send((&r[0]+ (i* sizeproc)),domain[1]*numgridpoints_x,MPI_DOUBLE,i,0,new_comm);
        MPI_Send((&u[0]+ (i* sizeproc)),domain[1]*numgridpoints_x,MPI_DOUBLE,i,1,new_comm);


      }
    }
    //*****************************************************************
    //* Distribution of r and u if domain is decomposed into second or third form 

    if(domain[4]==2 || domain[4]==3)
    {
      sizeproc= domain[1] * numgridpoints_x;
      int sizeproc_2= domain[3] * numgridpoints_x;
        
      for (auto i = 1; i < size-1; ++i)
      {
        MPI_Send((&r[0]+ (i* sizeproc)),sizeproc,MPI_DOUBLE,i,0,new_comm);
        MPI_Send((&u[0]+ (i* sizeproc)),sizeproc,MPI_DOUBLE,i,1,new_comm);

      }
        MPI_Send((&r[0] +((size-1)* sizeproc)),sizeproc_2,MPI_DOUBLE,size-1,0,new_comm);
        MPI_Send((&u[0] +((size-1)* sizeproc)),sizeproc_2,MPI_DOUBLE,size-1,1,new_comm);
     }
}
  else
  {
    //MPI_Status status1,status2;
    MPI_Recv(&r[0],sizeproc,MPI_DOUBLE,0,0,new_comm,&status);
    MPI_Recv(&u[0],sizeproc,MPI_DOUBLE,0,1,new_comm,&status);
  }  


  //******************************************
  //****** Set all data arrays for rank 0 ****
  //******************************************
  // Rank 0 processor will be working on size_proc always ...whatever be the domain decomposition ....


  //************************************************
  //*** ASSIGNMENT OF D ****************************
  //************************************************
  
  if( down< 0)
      {
        //std::cout<<"i am rank "<<cart_rank<<std::endl;
        for (auto i = 0; i < sizeproc; ++i)
        {
          d[i]=r[i] ;// Assign r to d 
          //std::cout<<d[i]<<std::endl;
        }
  
      }
  
  else
  {
    //std::cout<<"I am rank  "<<cart_rank<<std::endl;
     for (auto i =0; i < sizeproc; ++i)
   {
      // numgridpooints_x are left for lower ghost cell entries
      d[i+numgridpoints_x]=r[i];
    }
  }
 
  //************************************************
  //***  Compute Inner Indexes ****************************
  //************************************************

  if(up<0 || down <0)
    {  
      new_x= numgridpoints_x;
      new_y = (sizeproc/numgridpoints_x) + 1;
    }
    else
    {
      new_x = numgridpoints_x;
      new_y = (sizeproc/numgridpoints_x) + 2;
    }

    calInnerGridIndexes(new_x,new_y,proc_ind);

  //*************************************************
  //***** CG LOOP ***********************************
  //*************************************************z
  
  //*******************************************
    //Start the timer
    //*******************************************
    
    double timeTaken;
    MPI_Barrier(new_comm);
    siwir::Timer timer;  // Constructor called for timer object and the value of start_ has been set
  
  int count=0;

  while(count < num_iter)
  {

    scalar_Product_d_z=0.;
    scalar_Product_r_r=0.;

    if(cart_rank == 0)  
    {
      global_scalar_Product_d_z=0.;
    }

    //********************
    // Communicate D******
    //********************

    MPI_Request request[6];
    
    
    //****************
    // send up ******
    //****************
    if(cart_rank ==0)
    {
      MPI_Isend((d.data()+sizeproc-numgridpoints_x),numgridpoints_x,MPI_DOUBLE,up,2,new_comm,&request[0]);
    }

    else if(up>0)
    {
      MPI_Isend(d.data()+sizeproc,numgridpoints_x,MPI_DOUBLE,up,2,new_comm,&request[1]);
    }

    MPI_Barrier(new_comm);

    
    //*********************
    // Receive from down***
    //*********************

    if(down>=0 )
    {
      MPI_Recv(d.data(),numgridpoints_x,MPI_DOUBLE,down,2,new_comm,&status);
    }
    MPI_Barrier(new_comm);
 
    //****************************
    //**Send down ****************
    //****************************
    if(down>=0)
    {
      MPI_Isend(d.data()+numgridpoints_x,numgridpoints_x,MPI_DOUBLE,down,3,new_comm,&request[2]);
    }

    //************************
    // RECEIVE FROM UP *******
    //************************
    MPI_Barrier(new_comm);
    
    if(cart_rank==0)
    {
      MPI_Recv(d.data()+ sizeproc,numgridpoints_x,MPI_DOUBLE,up,3,new_comm,&status);
    }

    else if(up>0)
    {
      MPI_Recv( d.data()+ sizeproc + numgridpoints_x,numgridpoints_x,MPI_DOUBLE,up,3,new_comm,&status);
    }

    MPI_Barrier(new_comm);

    //*********************************************
    // Compute z = Ad ******************************
    //********************************************* 

    for (unsigned int i = 0; i < proc_ind.size(); ++i)
    {
      double d_left= d[proc_ind[i]-1];
      double d_right= d[proc_ind[i]+1];
      double d_up= d[proc_ind[i]+numgridpoints_x];
      double d_down = d[proc_ind[i]-numgridpoints_x];
      double d_center= d[proc_ind[i]];

      z[proc_ind[i]] =  Compute_Matrix_Vec_Mult(d_center,d_left,d_right,d_up,d_down,constant,hxsqinv,hysqinv);

    }

    //*********************************************
    // Compute alpha = delta_0/ (d,z)  ************
    //********************************************* 
    
    for(auto i=0;i<sizeproc+2*numgridpoints_x;++i)
    {
      scalar_Product_d_z +=  d[i]*z[i];
    }

    // Wait untill all processors have computed their (d,z)
    MPI_Barrier(new_comm);

    // saves the sum of indivisual scalar product of processors in global_scalar_Product_d_z
    MPI_Reduce(&scalar_Product_d_z, &global_scalar_Product_d_z,1,MPI_DOUBLE,MPI_SUM,0,new_comm);

    if(cart_rank == 0)
      {
        alpha = delta_0/global_scalar_Product_d_z;
      }

    // Broadcast the alpha computed by 0 processor to all the processors
    MPI_Bcast( &alpha,1, MPI_DOUBLE, 0, new_comm);


    //*********************************************
    // u = u+ alpha*d **************************
    // r = r-alpha*z  ******************************
    //********************************************* 
    
    for(auto i=0; i<sizeproc;i++)
    {
      if(cart_rank == 0)
      {
        u[i]+= alpha*d[i];
        r[i] -= alpha*z[i];
      }
      else 
      { 
        u[i] += alpha*d[i+numgridpoints_x];  
        r[i] -= alpha*z[i+numgridpoints_x];
      }

    }

    for(auto i=0;i<sizeproc; ++i)
      scalar_Product_r_r += r[i]*r[i]; 

    MPI_Barrier(new_comm);
    
    MPI_Reduce(&scalar_Product_r_r, &delta_1,1,MPI_DOUBLE,MPI_SUM,0,new_comm);

    MPI_Barrier(new_comm);
    
    if(cart_rank==0)
    {
     // if(terminate(sqrt(delta_1),tol))
      //std::cout << "delta_1 at: " << count + 1 << "is : " << delta_1 <<std::endl;
      if(delta_1 < tolsq)
      {  
        isSolutionConverged=1;
        std::cout << "Solution converged after " << count+ 1 << " iterations. Residual is  "<< delta_1 << std::endl;
        
      }
    }


    MPI_Barrier(new_comm);
    MPI_Bcast(&isSolutionConverged,1,MPI_INT,0,new_comm);
   
    //*********************************************
    //processor 0 will receive the u's form all ***
    // other processors ******************************
    //********************************************* 
    if(isSolutionConverged)
    {

      if(cart_rank !=0)
      {  
        MPI_Send(&u[0]+ (cart_rank*domain[1]*numgridpoints_x),sizeproc,MPI_DOUBLE,0,4,new_comm);
      }
      else
      {
        MPI_Status status_1; 
        for(auto i=1; i<size-1; ++i)
        {  
          MPI_Recv(&u[0]+ i*sizeproc, sizeproc,MPI_DOUBLE,i,4,new_comm,&status_1);
        }

        if(domain[4] == 1)
          MPI_Recv(&u[0]+ (size-1)*sizeproc, sizeproc,MPI_DOUBLE,size-1,4,new_comm,&status_1);
        else   
          MPI_Recv(&u[0]+ (size-1)*sizeproc, domain[3]*numgridpoints_x,MPI_DOUBLE,size-1,4,new_comm,&status_1);
      }

      MPI_Barrier(new_comm);

      break;
    }

   // Computes beta 
   if(cart_rank == 0)
   { 
    beta= delta_1/delta_0;
   }

  MPI_Bcast(&beta,1,MPI_DOUBLE,0,new_comm);

  if(cart_rank==0)
  {
    for(auto i=0;i<sizeproc; ++i)
    {
        d[i]=r[i]+beta*d[i]; 
    }
  }
  else
  {
    for (int i = 0; i < sizeproc; ++i)
    {
        d[i+numgridpoints_x]=r[i]+beta*d[i+numgridpoints_x];
    }
  }  

  // Update the following values to be used in the next loop
   count++;
  
  if(cart_rank == 0) 
  {
    delta_0 = delta_1; 
    if((count == num_iter) && (isSolutionConverged == 0))
      std::cout << " The solution has not converged after " << count << " ierations.\n";
  } 
  
  MPI_Barrier(new_comm);
  
}
  
  MPI_Barrier(new_comm);  
  // End the timer here
  if(cart_rank ==0)
  {
  timeTaken = timer.elapsed();
  std::cout << "Total time taken in MPI Run is: " << timeTaken << " seconds.\n";
  }


  if(cart_rank == 0)    
  write_sol(u,hx,hy,numgridpoints_x,numgridpoints_y);
  
   
  MPI_Finalize();

return 0;
}
//****************************************
// Domain decomosition 
//****************************************
void domaindecompose(const size_t &numy,const int &size,int domain[])
{
	//std::cout<<"Domain Decmposition starts for 1D array of processor"<<std::endl;	
	size_t dataprocydirect = 0;
	//int numprocxdirect = 1;
	if(numy%size ==0)
	{
		dataprocydirect = numy / size;
		domain[0] = size;
		domain[1] = dataprocydirect;
		domain[2] = 0;
		domain[3] = 0;
    domain[4] = 1;
		//std::cout<<"Domain is Decomposed into equal subdomains "<<std::endl;
	}
  else
  {
    if(numy%(size-1)!=0 && (!((numy-((numy/(size-1))*(size-1))) == 1 )))
    {
      dataprocydirect = numy / (size-1);
      domain[0] = size-1;
      domain[1] = dataprocydirect;
      domain[2] = 1;
      domain[3] = numy- (domain[0] * domain[1]);
      domain[4] = 2;
      //std::cout<<"Domain is  Decomposed into irregular domains 2nd form "<<std::endl;
    }
    else
    {
      dataprocydirect = numy / size;
      domain[0] = size-1;
      domain[1] = dataprocydirect;
      domain[2] = 1;
      domain[3] = numy - (domain[0]*domain[1]);
      domain[4] = 3;
      //std::cout<<"Domain is Decomposed into irregular subdomains 3rd form"<<std::endl;
    }

  }
  
	
}
//******************************
// Boundary value Initialisation 

inline void Boundary_Init(const int &numx,const int &numy,const double hx,std::vector<double> &u){
  // Implementig the boundary value
  for (auto i = (numy-1) * numx; i < (numx * numy) ; ++i ) {
      u[i] = (con_sinh* sin(2 * PI * (hx *(i - ((numy - 1) * numx)))));
  }

}
//******************************

//******************************
// Functional Initialisation 

inline void func_init(const int &numx,const int &numy,const double &hx,const double &hy,std::vector<double>& f){
  // Updating the function value on any domain
  double x,y;
  for (auto i = 0; i < numx * numy; ++i) {
    x = hx * (i % numx);
    y = hy * (i / numx);
    f[i] = 4 * PI *PI * sin(2 * PI * x) * sinh(2* PI * y );
    //std::cout << "The value of f is"<<f[i]<< std::endl;

  }
}

//******************************

//******************************
// Compute Matrix vector Multiplication 

inline double Compute_Matrix_Vec_Mult(const double &u_center,const double &u_left,const double &u_right,const double &u_up,const double &u_down,const double &constant,const double &hxsqinv,const double &hysqinv){

  return(((u_center * (constant) - (((hxsqinv)*(u_left + u_right)) + (hysqinv)*( u_up + u_down)))));
}

//******************************
// Write solution to file 
inline void write_sol(const std::vector<double> &u_temp,const double hx,const double hy,const int numx,const int numy){

  double x,y;
  const int numGridPoints = numx*numy;
  std::ofstream file_;
  file_.open("solution.txt");
  for (auto i = 0; i < numGridPoints; ++i) 
  {
    x = hx * (i % numx);
    y = hy * (i / numx);
    file_<<x<<"  "<<y<<"  "<<u_temp[i]<<std::endl;
  }
  file_.close();
  std::cout << "The solution has been written in solution.txt" << std::endl;

}

//*******************************
// Terminating function 
bool terminate(const double value, const double tol){
  if(value<tol){
    return true;
  }
  else
    return false;
}

//*****************************
// Scalar product function 
inline double cal_Scalar_Product(const std::vector<double> &temp_r,const int size){
  // Calculating the norm of the vector
  double sum(0);
  for (auto i = 0; i < size; i++) {
    // Computing the Norm
    sum+= temp_r[i] * temp_r[i];
  }
  return (sum);
}


//******************************
// Correct idex calculator Function. It calculates the inner points in any domain

void calInnerGridIndexes(const int &numGridX,const int &numGridY,std::vector<int>& ind) {
  
    for(auto i=1; i<numGridY - 1; ++i) {
       for(auto j=1 ;j<numGridX-1;++j) {
        ind.push_back(i*numGridX +j);
      }
    }
 }

//************************************






