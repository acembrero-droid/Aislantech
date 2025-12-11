// Mobile menu toggle
const mobileMenuBtn = document.getElementById('mobileMenuBtn');
const mainMenu = document.getElementById('mainMenu');

if (mobileMenuBtn && mainMenu) {
    mobileMenuBtn.addEventListener('click', () => {
        mainMenu.classList.toggle('active');
        mobileMenuBtn.innerHTML = mainMenu.classList.contains('active') 
            ? '<i class="fas fa-times"></i>' 
            : '<i class="fas fa-bars"></i>';
    });
}

// Smooth scroll for anchor links
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function(e) {
        e.preventDefault();
        
        // Close mobile menu if open
        if(mainMenu && mainMenu.classList.contains('active')) {
            mainMenu.classList.remove('active');
            mobileMenuBtn.innerHTML = '<i class="fas fa-bars"></i>';
        }
        
        const targetId = this.getAttribute('href');
        if(targetId === '#') return;
        
        const targetElement = document.querySelector(targetId);
        if(targetElement) {
            window.scrollTo({
                top: targetElement.offsetTop - 80,
                behavior: 'smooth'
            });
        }
    });
});

// Project filtering
const filterBtns = document.querySelectorAll('.filter-btn');
const projectItems = document.querySelectorAll('.project-item');

if (filterBtns.length > 0 && projectItems.length > 0) {
    filterBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            // Remove active class from all buttons
            filterBtns.forEach(b => b.classList.remove('active'));
            // Add active class to clicked button
            btn.classList.add('active');
            
            const filterValue = btn.getAttribute('data-filter');
            
            projectItems.forEach(item => {
                if(filterValue === 'all' || item.getAttribute('data-category') === filterValue) {
                    item.style.display = 'block';
                } else {
                    item.style.display = 'none';
                }
            });
        });
    });
}

// Service selection in contact form
const serviceOptions = document.querySelectorAll('.service-option');
if (serviceOptions.length > 0) {
    serviceOptions.forEach(option => {
        option.addEventListener('click', () => {
            serviceOptions.forEach(opt => opt.classList.remove('selected'));
            option.classList.add('selected');
        });
    });
}

// Project modal
const projectModal = document.getElementById('projectModal');
const modalClose = document.getElementById('modalClose');
const modalTitle = document.getElementById('modalTitle');
const modalBody = document.getElementById('modalBody');

// Open modal when project item is clicked
if (projectItems.length > 0) {
    projectItems.forEach(item => {
        item.addEventListener('click', () => {
            const category = item.getAttribute('data-category');
            const overlay = item.querySelector('.project-overlay');
            const title = overlay.querySelector('h4').textContent;
            const description = overlay.querySelector('p').textContent;
            
            let categoryText = '';
            switch(category) {
                case 'oficina': categoryText = 'Oficina Técnica'; break;
                case 'consultoria': categoryText = 'Consultoría Industrial'; break;
                case 'precerco': categoryText = 'Precerco Avanzado'; break;
                case 'solar': categoryText = 'Protección Solar'; break;
            }
            
            modalTitle.textContent = title;
            modalBody.innerHTML = `
                <div style="display: flex; flex-wrap: wrap; gap: 30px;">
                    <div style="flex: 1; min-width: 300px;">
                        <h4>Descripción del Proyecto</h4>
                        <p>${description}</p>
                        <p><strong>Categoría:</strong> ${categoryText}</p>
                        <p><strong>Cliente:</strong> Empresa Industrial S.A.</p>
                        <p><strong>Año:</strong> 2023</p>
                        <p><strong>Duración:</strong> 4 meses</p>
                    </div>
                    <div style="flex: 1; min-width: 300px;">
                        <h4>Resultados Obtenidos</h4>
                        <ul style="list-style: none; padding-left: 0;">
                            <li><i class="fas fa-check" style="color: var(--color-primary); margin-right: 8px;"></i> Optimización del 25% en tiempos de diseño</li>
                            <li><i class="fas fa-check" style="color: var(--color-primary); margin-right: 8px;"></i> Reducción del 15% en materiales empleados</li>
                            <li><i class="fas fa-check" style="color: var(--color-primary); margin-right: 8px;"></i> Mejora del 30% en eficiencia energética</li>
                            <li><i class="fas fa-check" style="color: var(--color-primary); margin-right: 8px;"></i> Cumplimiento total de normativas</li>
                        </ul>
                    </div>
                </div>
                <div style="margin-top: 30px;">
                    <h4>Tecnologías Aplicadas</h4>
                    <div style="display: flex; gap: 10px; margin-top: 10px; flex-wrap: wrap;">
                        <span style="background-color: var(--color-primary); color: var(--color-dark); padding: 5px 10px; border-radius: 4px; font-weight: 600;">Modelado 3D</span>
                        <span style="background-color: var(--color-secondary); color: white; padding: 5px 10px; border-radius: 4px; font-weight: 600;">Análisis BIM</span>
                        <span style="background-color: var(--color-primary-light); color: var(--color-dark); padding: 5px 10px; border-radius: 4px; font-weight: 600;">Simulación Solar</span>
                        <span style="background-color: var(--color-secondary-dark); color: white; padding: 5px 10px; border-radius: 4px; font-weight: 600;">Cálculo Estructural</span>
                    </div>
                </div>
            `;
            
            projectModal.classList.add('active');
        });
    });
}

// Close modal
if (modalClose) {
    modalClose.addEventListener('click', () => {
        projectModal.classList.remove('active');
    });
}

// Close modal when clicking outside
if (projectModal) {
    window.addEventListener('click', (e) => {
        if(e.target === projectModal) {
            projectModal.classList.remove('active');
        }
    });
}

// Form submission
const contactForm = document.getElementById('contactForm');
if (contactForm) {
    contactForm.addEventListener('submit', (e) => {
        e.preventDefault();
        
        // Get selected service
        let selectedService = '';
        serviceOptions.forEach(option => {
            if(option.classList.contains('selected')) {
                selectedService = option.getAttribute('data-service');
            }
        });
        
        if (!selectedService) {
            alert('Por favor, selecciona un servicio antes de enviar el formulario.');
            return;
        }
        
        // In a real application, here you would send the form data to a server
        alert(`Gracias por su consulta sobre ${selectedService}. Nos pondremos en contacto con usted en menos de 24 horas.`);
        contactForm.reset();
        serviceOptions.forEach(option => option.classList.remove('selected'));
    });
}

// Header scroll effect
window.addEventListener('scroll', () => {
    const header = document.querySelector('header');
    if(header) {
        if(window.scrollY > 100) {
            header.style.boxShadow = '0 5px 20px rgba(0, 0, 0, 0.1)';
        } else {
            header.style.boxShadow = '0 2px 10px rgba(0, 0, 0, 0.05)';
        }
    }
});

// Animation on scroll (simplified)
const observerOptions = {
    threshold: 0.1,
    rootMargin: '0px 0px -50px 0px'
};

const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if(entry.isIntersecting) {
            entry.target.style.opacity = '1';
            entry.target.style.transform = 'translateY(0)';
        }
    });
}, observerOptions);

// Observe elements for animation
document.querySelectorAll('.service-card, .timeline-item, .project-item, .feature-item').forEach(el => {
    el.style.opacity = '0';
    el.style.transform = 'translateY(20px)';
    el.style.transition = 'opacity 0.5s ease, transform 0.5s ease';
    observer.observe(el);
});

// Language selector toggle
const languageSelector = document.querySelector('.language-selector');
if (languageSelector) {
    languageSelector.addEventListener('click', () => {
        const currentLang = languageSelector.textContent.trim();
        if (currentLang === 'ES | EN') {
            languageSelector.textContent = 'EN | ES';
        } else {
            languageSelector.textContent = 'ES | EN';
        }
    });
}

// Initialize the page
document.addEventListener('DOMContentLoaded', function() {
    console.log('Página de Aislantech cargada correctamente');
});
